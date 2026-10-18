/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* runtime objects for a system's records. Each object gets
 * its size, scale, orbit and orientation; special records (kind 0x46:
 * stations, space ports...) are placed beside their parent instead. */
#include "systems_internal.h"

enum {
    OBJECT_CLASS_BODY = 0x1d,
    SIZE_FACTOR_MANTISSA = 0x4082, SIZE_FACTOR_EXPONENT = 0xffd5,
    ROOT_SCALE_STEP = 0x10, NEAR_PARENT_SCALE_STEP = 0x0a, LARGE_SCALE_STEP = 4,
    NEAR_PARENT_BYTES = 0x80,
    FLAGS_ROOT = 0x11, FLAGS_LARGE = 0x1f, FLAGS_SPECIAL = 0x0f,
    KIND_LARGE = 0x40, KIND_LARGE_SEVEN = 0x44,
    ORBIT_MANTISSA = 0x5460, ORBIT_EXPONENT = 0xffee, ORBIT_BIAS = 0x40,
    SWAPPED_BASIS_MODEL = 0x56,
    SPECIAL_MODEL_FIRST = 0x5a, SPECIAL_MODEL_LAST = 0x64,
    SPECIAL_MODEL_KEEPS_SHAPE_A = 0x5a, SPECIAL_MODEL_KEEPS_SHAPE_B = 0x5c,
    SPECIAL_MODEL_SMALL = 0x62,
    OBJECT_FLAG_LARGE = 0x10, OBJECT_FLAG_SPECIAL = 0x20, OBJECT_FLAG_DOCKABLE = 0x40,
    SPECIAL_ANGLE_OFFSET = 0x4000, PLACEMENT_BITS = 0x37
};

/* The object a record hangs from: a runtime object, or the static Frontier
 * object for top-level records. */
typedef struct parent_object {
    fe2_object *object;
} parent_object;

static uint8_t parent_handle(const parent_object *parent)
{
    return parent->object ? parent->object->handle : fe2_systems_static_object_handle;
}

static uint16_t parent_model(const parent_object *parent)
{
    return parent->object ? parent->object->model : fe2_systems_static_object_model;
}

static fe2_scalar scalar(uint16_t mantissa, uint16_t exponent)
{
    fe2_scalar s;
    s.mantissa = mantissa;
    s.exponent = exponent;
    return s;
}

static void set_shape(fe2_object *o, uint16_t shape)
{
    o->as.body.shape = shape;
}

static void set_radius_class(fe2_object *o)
{
    o->as.body.radius_class = (uint16_t)((o->as.body.shape + 1u) << 3);
}

/* orbit radius, wide orbit product, interpolated position and the
 * orientation of a body with a parent. */
static void place_in_orbit(fe2_system_generation *g, fe2_object *o, const fe2_system_record *r,
                           const parent_object *parent, const uint32_t orbit[2])
{
    const fe2_system_services *game = g->game;
    fe2_call_args args;
    fe2_scalar s;
    uint32_t time;
    uint16_t product[4], first, second;

    o->parent_handle = parent_handle(parent);
    s = game->normalize_word(game->opaque, scalar(fe2_record_word(r, FE2_RECORD_DERIVED_MANTISSA),
                                                  fe2_record_word(r, FE2_RECORD_DERIVED_EXPONENT)));
    s = game->divide(game->opaque, scalar(ORBIT_MANTISSA, ORBIT_EXPONENT), s);
    s.exponent = (uint16_t)(s.exponent + ORBIT_BIAS);
    o->as.body.orbit_radius = game->to_unsigned_dword(game->opaque, s);

    fe2_args_clear(&args);
    fe2_args_dword(&args, 4, o->as.body.orbit_radius);
    fe2_args_dword(&args, 0, 0);
    fe2_args_dword(&args, 1, 0);
    fe2_args_dword(&args, 2, orbit[0]);
    o->orbit[0] = orbit[0];
    fe2_args_dword(&args, 3, orbit[1]);
    o->orbit[1] = orbit[1];
    game->wide_product(game->opaque, &args, product);
    product[3] = (uint16_t)(product[3] + fe2_record_word(r, FE2_RECORD_ORBIT_LONGITUDE));
    o->as.body.orbit_product[0] = product[0];
    o->as.body.orbit_product[1] = product[1];
    o->as.body.orbit_product[2] = product[2];
    o->as.body.orbit_product[3] = product[3];
    fe2_args_dword(&args, 1, (uint32_t)product[3] << 16 | product[2]);
    o->as.body.orbit_inclination = fe2_record_word(r, FE2_RECORD_ORBIT_INCLINATION);
    fe2_args_word(&args, 0, o->as.body.orbit_inclination);
    fe2_args_object(&args, 8, o);
    fe2_args_word(&args, 6, (uint16_t)o->scalar);
    fe2_args_word(&args, 7, (uint16_t)(o->scalar >> 16));
    game->interpolate(game->opaque, o, &args);

    /* Orientation from the basis seed and the orbit time word doubled
     * time_shift - 1 times. */
    time = 0;
    if (o->as.body.time_shift != 0) {
        unsigned doublings = o->as.body.time_shift - 1u;
        time = o->orbit[0];
        while (doublings-- > 0) time <<= 1;
    }
    first = o->as.body.basis_seed;
    second = (uint16_t)(time >> 16);
    if (o->model == SWAPPED_BASIS_MODEL) {
        uint16_t swap = first;
        first = second;
        second = swap;
    }
    g->basis_angle[0] = first;
    g->basis_angle[1] = second;
    game->basis_b(game->opaque, o);
}

/* Arithmetic shift of the 64-bit value high:0 right by `count`. */
static void place_axis(uint32_t high, unsigned count, uint32_t *out_high, uint32_t *out_low)
{
    uint32_t low = 0;
    for (; count > 0; --count) {
        low = (low >> 1) | (high << 31);
        high = (high >> 1) | (high & 0x80000000u);
    }
    *out_high = high;
    *out_low = low;
}

/* a special object beside its parent, at a distance set by the
 * parent model's size. */
static void place_special(fe2_system_generation *g, fe2_object *o, const fe2_system_record *r,
                          const parent_object *parent)
{
    const fe2_system_services *game = g->game;
    uint16_t model = fe2_record_word(r, FE2_RECORD_DESCRIPTOR);
    const fe2_model_header *header;
    fe2_call_args args;
    uint32_t vector[3];
    unsigned axis, shift;
    int16_t distance;

    o->parent_handle = parent_handle(parent);
    o->flags |= OBJECT_FLAG_SPECIAL;
    if (model >= SPECIAL_MODEL_FIRST && model <= SPECIAL_MODEL_LAST) {
        o->flags |= OBJECT_FLAG_DOCKABLE;
        if (model != SPECIAL_MODEL_KEEPS_SHAPE_A && model != SPECIAL_MODEL_KEEPS_SHAPE_B) {
            set_shape(o, 5);
            if (model == SPECIAL_MODEL_SMALL) set_shape(o, 1);
        }
    }
    set_radius_class(o);
    game->set_object_flags(game->opaque, o, FLAGS_SPECIAL);
    o->station_slot = 0xff;
    o->as.body.orbit_radius = 0;
    g->basis_angle[0] = (uint16_t)(fe2_record_word(r, FE2_RECORD_ORBIT_LONGITUDE) - SPECIAL_ANGLE_OFFSET);
    g->basis_angle[1] = fe2_record_word(r, FE2_RECORD_ORBIT_INCLINATION);
    game->basis_a(game->opaque, o);

    header = game->model(game->opaque, parent_model(parent));
    distance = (int16_t)(PLACEMENT_BITS - header->radius[0] - header->radius[1]);
    fe2_args_clear(&args);
    fe2_args_dword(&args, 6, (uint32_t)header->scale << 16);
    for (axis = 0; axis < 3; ++axis)
        vector[axis] = (uint32_t)(uint16_t)o->orientation[3 * axis + 1] << 16;
    game->rescale_vector(game->opaque, vector, &args);
    shift = distance > 0 ? (unsigned)distance + 1u : 1u;
    for (axis = 0; axis < 3; ++axis)
        place_axis(vector[axis], shift, &o->position_high[axis], &o->position_low[axis]);
}

static parent_object parent_of(fe2_system_generation *g, unsigned list, uint16_t parent_index)
{
    parent_object parent;
    parent.object = 0;
    if (parent_index != 0) {
        const fe2_system_record *p = fe2_systems_record(g, list + parent_index - 1u);
        parent.object = g->game->object_from_reference(g->game->opaque,
                                                       fe2_record_dword(p, FE2_RECORD_PROFILE_OR_OBJECT_REFERENCE));
    }
    return parent;
}

/* Size, scale and class fields common to every record's object. */
static void initialize(fe2_system_generation *g, fe2_object *o, fe2_system_record *r,
                       unsigned list, unsigned record)
{
    const fe2_system_services *game = g->game;
    uint16_t parent_index = fe2_record_word(r, FE2_RECORD_PARENT);
    uint16_t kind = fe2_record_word(r, FE2_RECORD_KIND);
    uint32_t seed = fe2_record_dword(r, FE2_RECORD_SEED);
    fe2_scalar size;
    uint16_t scale;

    fe2_record_set_dword(r, FE2_RECORD_PROFILE_OR_OBJECT_REFERENCE, game->object_reference(game->opaque, o));
    o->target_seed = (seed >> 16) | (seed << 16);
    o->as.body.time_shift = fe2_record_byte(r, FE2_RECORD_ORBIT_SHIFT);
    o->as.body.generation = fe2_record_byte(r, FE2_RECORD_GENERATION);
    o->as.body.temperature = fe2_record_word(r, FE2_RECORD_TEMPERATURE);
    o->as.body.basis_seed = fe2_record_word(r, FE2_RECORD_BASIS_SEED);
    size = game->multiply(game->opaque, scalar(fe2_record_word(r, FE2_RECORD_SIZE_MANTISSA),
                                               fe2_record_word(r, FE2_RECORD_SIZE_EXPONENT)),
                          scalar(SIZE_FACTOR_MANTISSA, SIZE_FACTOR_EXPONENT));
    size = game->normalize_word(game->opaque, size);
    o->size_mantissa = size.mantissa;
    o->size_exponent = size.exponent;
    scale = size.exponent;
    if (kind == 0) {
        scale = (uint16_t)(scale + ROOT_SCALE_STEP);
        game->set_object_flags(game->opaque, o, FLAGS_ROOT);
    }
    if (parent_index != 0 &&
        fe2_record_word(fe2_systems_record(g, list + parent_index - 1u), FE2_RECORD_KIND) == 0 &&
        (int16_t)(uint16_t)((record - list - parent_index) * FE2_RECORD_BYTES) < NEAR_PARENT_BYTES)
        scale = (uint16_t)(scale - NEAR_PARENT_SCALE_STEP);
    o->as.body.scale_exponent = scale;
    o->scalar = fe2_record_dword(r, FE2_RECORD_SCALAR_MANTISSA);
    o->role = 1;
}

void fe2_systems_instantiate(fe2_system_generation *g, const uint32_t orbit[2])
{
    const fe2_system_services *game = g->game;
    unsigned list = g->destination, record;

    for (record = list;; record = (record + 1u) % g->space.count) {
        fe2_system_record *r = fe2_systems_record(g, record);
        uint16_t parent_index = fe2_record_word(r, FE2_RECORD_PARENT);
        int16_t kind = (int16_t)fe2_record_word(r, FE2_RECORD_KIND);
        parent_object parent = parent_of(g, list, parent_index);
        fe2_call_args args;
        fe2_object *o;

        if (fe2_record_dword(r, FE2_RECORD_SIZE_MANTISSA) == 0) return;
        fe2_args_clear(&args);
        fe2_args_word(&args, 0, OBJECT_CLASS_BODY);
        fe2_args_word(&args, 1, fe2_record_word(r, FE2_RECORD_DESCRIPTOR));
        if (parent.object != 0) fe2_args_object(&args, 8, parent.object);
        else fe2_args_template(&args, 8, fe2_systems_static_object);
        o = game->allocate_object(game->opaque, &args);
        if (o == 0) return;
        initialize(g, o, r, list, record);

        if (kind >= KIND_LARGE) {
            o->as.body.scale_exponent = (uint16_t)(o->as.body.scale_exponent + LARGE_SCALE_STEP);
            o->flags |= OBJECT_FLAG_LARGE;
            set_shape(o, 3);
            game->set_object_flags(game->opaque, o, FLAGS_LARGE);
            if (kind == KIND_LARGE_SEVEN) set_shape(o, 7);
            o->as.body.docking_claim = 0;
            o->as.body.docking_state = 0;
            o->as.body.docking_timer = 0;
            if (kind == FE2_RECORD_KIND_VERBATIM) {
                place_special(g, o, r, &parent);
            } else {
                set_radius_class(o);
                if (parent_index != 0) place_in_orbit(g, o, r, &parent, orbit);
            }
        } else if (parent_index != 0) {
            place_in_orbit(g, o, r, &parent, orbit);
        }
        {
            unsigned i;
            const char *name = fe2_record_name_const(r);
            for (i = 0; i < sizeof o->name; ++i)
                if ((o->name[i] = name[i]) == 0) break;
        }
    }
}

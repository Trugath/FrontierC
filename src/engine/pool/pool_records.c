/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* a new record from its template and model. */
#include "math/math3.h"
#include "game/model.h"
#include "pool.h"

enum {
    UNSET_LEVEL = 0xff, MARKED_VARIANT = 0x80, SCALE_EXPONENT_BIAS = 7,
    CLASS_ONE_LIMIT = 0xaa5, CLASS_TWO_LIMIT = 0x1329, DRIVE_FITTED = 0x8000
};

/* A 48-bit scale: the model's mantissa at the model's radius exponent. */
static void model_scale(const uint8_t *model, unsigned field, uint16_t out[3])
{
    fe2_scalar v;
    uint64_t wide;
    v.mantissa = fe2_model_word(model, field);
    v.exponent = (uint16_t)(fe2_model_word(model, FE2_MODEL_RADIUS) + fe2_model_word(model, FE2_MODEL_RADIUS_EXTRA) +
                            SCALE_EXPONENT_BIAS);
    wide = (uint64_t)fe2_math_to_signed64(v);
    out[0] = (uint16_t)wide;
    out[1] = (uint16_t)(wide >> 16);
    out[2] = (uint16_t)(wide >> 32);
}

/*
 * The flight parameters: the third control's positive limit, the others'
 * +-limit, the control class by that limit, the capacity class, hull, and
 * the capacity less a fitted drive's mass.
 */
static void flight_parameters(fe2_object *o, const uint8_t *flight)
{
    int16_t limit = (int16_t)fe2_model_word(flight, FE2_FLIGHT_LIMIT), negated = (int16_t)(uint16_t)-(uint16_t)limit;
    uint16_t capacity = fe2_model_word(flight, FE2_FLIGHT_CAPACITY), drive = fe2_model_word(flight, FE2_FLIGHT_DRIVE);
    o->as.flight.control_limits[2][0] = (int16_t)fe2_model_word(flight, FE2_FLIGHT_THIRD_LIMIT);
    o->as.flight.control_limits[0][1] = o->as.flight.control_limits[1][1] = o->as.flight.control_limits[2][1] = limit;
    o->as.flight.control_limits[0][0] = o->as.flight.control_limits[1][0] = negated;
    o->control_class = negated >= CLASS_TWO_LIMIT ? 2 : negated >= CLASS_ONE_LIMIT ? 1 : 0;
    o->as.flight.capacity_class = (uint8_t)fe2_model_word(flight, FE2_FLIGHT_CAPACITY_CLASS);
    o->hull = (uint16_t)(fe2_model_word(flight, FE2_FLIGHT_HULL) << 2);
    if (drive & DRIVE_FITTED) {
        o->as.flight.drive = (uint8_t)drive;
        capacity = (uint16_t)(capacity - fe2_drive_adjustments[(uint16_t)(drive << 1) >> 1]);
    }
    o->capacity = capacity;
    o->as.flight.shield_charge = 0;
    o->as.flight.shield_capacity = 0;
}

static void from_model(const fe2_pool_state *s, const fe2_pool_services *game, fe2_object *o, uint16_t model_index)
{
    const uint8_t *model;
    uint16_t marker, flight;
    uint8_t variant = 0;
    o->orbit[0] = s->clock[0];
    o->orbit[1] = s->clock[1];
    o->model = model_index;
    model = game->model(game->opaque, model_index);
    model_scale(model, FE2_MODEL_SCALE_A, o->model_scale_a);
    model_scale(model, FE2_MODEL_SCALE_B, o->model_scale_b);
    o->frame_state = 0;
    o->render_state = 0;
    o->name[0] = 0;
    marker = fe2_model_word(model, FE2_MODEL_MARKER);
    if (marker != 0 && model[marker] != 0) variant = MARKED_VARIANT;
    variant |= (uint8_t)(model[FE2_MODEL_VARIANTS] >> 4);
    o->flags = (uint16_t)((o->flags & 0xff00u) | variant);
    o->level = UNSET_LEVEL;
    flight = fe2_model_word(model, FE2_MODEL_FLIGHT);
    if (flight != 0) flight_parameters(o, model + flight);
}

/* The template is copied in unless it is the record itself; a nonzero
 * model then initializes it. The record takes `handle`. */
uint8_t fe2_pool_initialize_record(fe2_pool_state *s, const fe2_pool_services *game, const fe2_new_record *request,
                                   fe2_object *record, uint8_t handle, int copy)
{
    if (copy && request->template_record != record) *record = *request->template_record;
    if (request->model != 0) from_model(s, game, record, request->model);
    record->handle = handle;
    return handle;
}

uint8_t fe2_pool_copy_template(fe2_pool_state *s, const fe2_pool_services *game, const fe2_new_record *request,
                               fe2_object *record)
{
    return fe2_pool_initialize_record(s, game, request, record, request->template_record->handle, 1);
}

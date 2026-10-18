/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* the contact scan. */
#include "contact_internal.h"

enum {
    CANDIDATE_SLOT = 0x04, FRAME_SLOT = 0x10,  /* slot status: a contact candidate; a frame record */
    NEAR_SPHERE = 0x02,                        /* the parent classification: within the parent's sphere range */
    PAIRED_MODEL = 0xb8,                       /* a model that meets only its own kind */
    COUNTDOWN_RELOAD = 0xffff, CLASSIFICATION_BITS = 0x1f
};

static int64_t position_of(const fe2_object *o, unsigned i)
{
    return (int64_t)((uint64_t)o->position_high[i] << 32 | o->position_low[i]);
}

/* Against the parent: its countdown runs down by half the frame step (a
 * borrow clears it); once clear, a record near the parent's sphere enters
 * its basis frame and one away from it leaves, reloading the countdown. */
static void classify_parent(fe2_contact *c, const fe2_contact_services *game, fe2_object *o)
{
    fe2_object *parent = fe2_pool_record(c->pool, o->parent_handle);
    int64_t position[3];
    uint8_t flags;
    unsigned i;
    c->reported = parent;
    for (i = 0; i < 3; ++i) position[i] = position_of(o, i);
    flags = fe2_contact_classify(c, game, o, parent, position);
    if (o->transition_countdown != 0) {
        uint16_t step = (uint16_t)(c->frame_step >> 1);
        if (o->transition_countdown >= step) {
            o->transition_countdown = (uint16_t)(o->transition_countdown - step);
            return;
        }
        o->transition_countdown = 0;
    }
    if (flags & NEAR_SPHERE) {
        if (o->station_slot != 0) return;
        fe2_motion_enter_basis_frame(o, parent);
    } else {
        if (o->station_slot == 0) return;
        fe2_motion_leave_basis_frame(game->motion, o, parent);
    }
    o->transition_countdown = COUNTDOWN_RELOAD;
}

/* Candidates (slot status 4, not frame records, not the record's own
 * handle) that are idle (+0x61 zero) and share the record's parent; model
 * 0xb8 only meets another 0xb8. In the same frame the positions subtract;
 * otherwise the position service relates them. */
fe2_contact_report fe2_contact_scan(fe2_contact *c, const fe2_contact_services *game, fe2_object *o, int respond)
{
    fe2_contact_report report;
    uint8_t parent;
    int slot;
    c->response = respond ? 0xff : 0;
    c->result = c->sphere = c->model_result = c->handle = 0;
    c->classification = 0;
    c->distance = UINT32_C(0xffffffff);              /* no gap measured */
    if (o->parent_handle != 0) classify_parent(c, game, o);
    parent = o->parent_handle;
    for (slot = FE2_POOL_LAST_SLOT; slot >= 0; --slot) {
        uint8_t status = c->pool->status[slot];
        fe2_object *candidate;
        int64_t relative[3];
        unsigned i;
        if (!(status & CANDIDATE_SLOT) || (status & FRAME_SLOT) || slot == o->handle) continue;
        candidate = fe2_pool_record(c->pool, (uint8_t)slot);
        if (candidate->update_countdown != 0 || candidate->parent_handle != parent) continue;
        if (candidate->model == PAIRED_MODEL && o->model != PAIRED_MODEL) continue;
        if (o->station_slot == candidate->station_slot) {
            for (i = 0; i < 3; ++i)
                relative[i] = (int64_t)((uint64_t)position_of(o, i) - (uint64_t)position_of(candidate, i));
        } else {
            game->frame_difference(game->opaque, o, candidate, relative);
        }
        fe2_contact_classify(c, game, o, candidate, relative);
    }
    report.flags = (uint8_t)(c->result | c->sphere);
    report.handle = c->handle;
    report.record = c->reported;
    report.distance = c->distance;
    report.classification = c->classification & CLASSIFICATION_BITS;
    return report;
}

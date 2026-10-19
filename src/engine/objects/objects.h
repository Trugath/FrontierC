/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_FRONTIER_OBJECTS_H
#define FE2_FRONTIER_OBJECTS_H

/*
 * Resident object services of the main executable: the random generator and
 * overlay selection (image 0x645..0x6f0), capacity recovery and propulsion
 * fuel, timed effects, missile and mine deployment, the name
 * editor and servicing failures, equipment activation
 *  and damage and debris.
 *
 * Functions take records (`fe2_object`) and the state (`fe2_objects_state`:
 * the game globals they read and write). The pool, the model bank, overlay
 * entries, sound, the math library, the scene probe and text expansion are
 * services (`fe2_objects_services`). Overlay entries are named by their
 * slot (the original's table offset / 8) and entry offset.
 */
#include <stddef.h>
#include <stdint.h>

#include "game/object.h"

enum {
    FE2_OBJECTS_MESSAGE_SLOT = 12, /* in-flight messages (entry +0x20) */
    FE2_OBJECTS_PANEL_SLOT = 15,   /* the control panel (entry +0x20) */
    FE2_OBJECTS_CARGO_WORDS = 31,
    /* Cargo items with their own uses. */
    FE2_CARGO_STANDARD_FUEL = 19,
    FE2_CARGO_MILITARY_FUEL = 20,
    FE2_CARGO_SPENT_FUEL = 29,     /* radioactives from spent military fuel */
    FE2_CARGO_TOTAL = 30,
    FE2_OBJECTS_PREAMBLE_BYTES = 0x9b
};

typedef struct fe2_objects_state {
    uint32_t random[2];
    uint16_t effect_seed[2];     /* the basis seed of the last effect or debris */
    uint8_t player;              /* the player's handle */
    uint8_t warnings;            /* bit 0 fuel, bit 2 weapons (once each) */
    uint8_t equipment_code;      /* the last activated equipment byte */
    uint16_t free_capacity;
    uint16_t cargo_space;
    uint16_t cargo[FE2_OBJECTS_CARGO_WORDS]; /*, by cargo item (FE2_CARGO_*) */
    uint32_t bounty;
    uint32_t rating;
    uint16_t lost_controls[6];   /* the player's control limits lost to damage */
    int16_t probe_parameters[4];
    uint32_t servicing;
    int16_t equipment_threshold;
    int16_t drive_threshold;
    uint32_t time_step;
    uint32_t simulation_step;    /* DS:0x56e7: this frame's captured simulation */
    const fe2_object *player_record;
    uint16_t data_segment;       /* the loaded data segment: the relocated word at
                                  * equipment kind 0x16's strength */
    uint16_t selected_overlay;   /* the overlay selection slot */
} fe2_objects_state;

/* New object allocation request. */
typedef struct fe2_objects_request {
    uint16_t flags, model;
    fe2_object *source;          /* the template/source record */
} fe2_objects_request;

/* An overlay message: selector, parameter and value
 * and amount  are passed when `uses` names them. */
enum { FE2_OBJECTS_USES_PARAMETER = 1, FE2_OBJECTS_USES_VALUE = 2, FE2_OBJECTS_USES_AMOUNT = 4 };
typedef struct fe2_objects_message {
    uint16_t selector, parameter, value;
    uint32_t amount;
    unsigned uses;
} fe2_objects_message;

/* The equipment probe : from the source along direction, offset by
 * offset, owned by `owner`. Results: the record hit, the reach,
 * the hit point (low dwords first) and the distance. */
typedef struct fe2_objects_probe {
    int32_t offset[3];
    int16_t direction[3];
    uint8_t owner;
    uint32_t range;              /* in: 0x7fffff; its second byte names the attacker */
    int hit;
    fe2_object *target;
    uint32_t reach, position[6], distance;
} fe2_objects_probe;

typedef struct fe2_objects_services {
    void *context;
    /* Overlay `slot`'s entry +0x20 with the slot's lock byte held. */
    void (*message)(void *context, unsigned slot, const fe2_objects_message *message);
    void (*probe)(void *context, fe2_object *source, fe2_objects_probe *probe);
    /* Notify subscribed object owners of damage while their lock is held. */
    void (*damage_callbacks)(void *context, fe2_object *record);
    /* an event to every overlay. */
    void (*broadcast)(void *context, uint16_t event);
    /* the active bank's descriptor of `model` (bytes from its start;
     * its word +0x1c is the extension's relative offset). */
    const uint8_t *(*model)(void *context, uint16_t model);
    /* (any nonzero slot) or 0x994 (upper slots): a new record, or 0. */
    fe2_object *(*allocate)(void *context, const fe2_objects_request *request, int upper);
    /* Object creation request flags. */
    void (*release)(void *context, fe2_object *record, uint16_t request);
    /* Look up `handle` in the root pool. */
    fe2_object *(*lookup)(void *context, uint8_t handle);
    /* The saved pool's status byte of `handle`. */
    uint8_t (*status)(void *context, uint8_t handle);
    /* a sound at the record's level; 0x340: a sound without level. */
    void (*sound)(void *context, uint16_t sound, uint8_t level);
    void (*sound_plain)(void *context, uint16_t sound);
    /* the record's basis from effect_seed. */
    void (*basis)(void *context, fe2_object *record, const uint16_t seed[2]);
    /* move the record by a 32-bit delta. */
    void (*move)(void *context, fe2_object *record, const int32_t delta[3]);
    /* 0x14: expand a text selector (zero-terminated) into out. */
    void (*text)(void *context, uint16_t selector, char *out, size_t capacity);
    /* 0x48: the next key, or -1 when none is waiting; 0x104: draw text. */
    int (*key)(void *context);
    void (*draw_text)(void *context, const char *text, uint8_t color, uint16_t argument);
} fe2_objects_services;

/* Serialized resource prefix, including compatibility bytes used by the
 * equipment-strength lookup. */
extern const uint8_t *fe2_objects_preamble;
int fe2_objects_bind(const uint8_t *image, size_t count);

/* ---- random.c: 0x645..0x6f0 --------------------------------------------- */
/* advance; returns the new second word (EAX: the first word rotated,
 * its sign gates effects) and, when mix is given, the value added to the
 * first (EDX). (The routine's header comment says EAX keeps the old first
 * word; the code rotates it.) */
uint32_t fe2_objects_random(fe2_objects_state *s, uint32_t *mix);
/* a random value below range (the high word of range * a new word). */
uint16_t fe2_objects_random_below(fe2_objects_state *s, uint16_t range);
/* sum (BX:CX, low word first) += pair (SI:DI); pair is rotated left
 * by five; sum += pair again. Returns AX * the sum's high word (AX the
 * product's high word, DX its low word, as the original leaves them). */
uint32_t fe2_objects_scale_pair(uint16_t ax, uint32_t *sum, uint32_t *pair);
/* select an overlay; a previous selection gets entry +0x24, AX = 0. */
void fe2_objects_select_overlay(fe2_objects_state *s, uint16_t slot,
                                void (*notify)(void *context, uint16_t previous), void *context);

/* ---- propulsion.c: 0x1f0d..0x25cd ----------------------------------------- */
/* recover the shields (0xb4 toward 0xb6) and optionally the hull,
 * expire the trail and effect, reduce the busy time, then consume fuel in
 * proportion to the controls. Returns the fuel consumed (DX:AX; people reads
 * AL as the capacity used). */
uint32_t fe2_objects_update(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, uint16_t quantum);
/* consume fuel from the reserve, refilling once from the cargo. */
void fe2_objects_consume(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, uint32_t amount);
/* a timed effect at position[0..5], variants by
 * flags. */
void fe2_objects_spawn_effect(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *source,
                              const uint32_t position[6], uint16_t flags);
/* launch the missile or mine of deployment slot `slot` at `target`.
 * Returns the new record, or 0 when the slot is empty or allocation fails. */
fe2_object *fe2_objects_deploy(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *source,
                               uint16_t slot, uint8_t target);
/* advance the servicing countdown by amount; each period may fail
 * equipment or the drive. */
void fe2_objects_service(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, uint32_t amount);
/* remove a random fitted item (it becomes damaged). Returns its text
 * selector in the generated item table, 0 when none was chosen. */
uint16_t fe2_objects_remove_equipment(fe2_objects_state *s, fe2_object *o);
/* fire installed item `index` (signed); 0x1952: damage a record by
 * amount from attacker (low byte the handle; the word is the release
 * request when it is destroyed). Returns 1 while it survives, 0 when it is
 * destroyed and released. */
void fe2_objects_activate(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, int8_t index);
int fe2_objects_damage(fe2_objects_state *s, const fe2_objects_services *v, fe2_object *o, uint16_t amount,
                       uint16_t attacker);

/* edit text in place (capacity characters), redrawing through
 * draw_text; mode bit 15 refuses spaces and parentheses. */
void fe2_objects_edit_text(const fe2_objects_services *v, char *text, uint16_t capacity, uint16_t argument,
                           uint16_t mode);

#endif

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_GAME_OBJECT_H
#define FE2_GAME_OBJECT_H

/*
 * Runtime object state shared by simulation, rendering and gameplay. Its C
 * layout is independent of the serialized object record decoded at boundaries.
 *
 * Craft (ships and temporary camera frames) and celestial bodies (planets,
 * stations and the static root) use different union views: `as.craft` and
 * `as.body`. A module uses the view of the objects it handles.
 */
#include <stdint.h>

enum { FE2_OBJECT_NAME_BYTES = 20, FE2_INSTALLED_ITEMS = 14 };

/* These three native union views share common fields. Prevent compiler padding
 * from moving a shared field to a different union offset in another view. */
#pragma pack(push, 1)

typedef struct fe2_craft_fields {
    uint16_t camera_flags;      /* camera frames only */
    uint8_t registration[12];
    uint32_t equipment;         /* fitted equipment bits */
    uint32_t equipment_damaged;
    uint8_t drive;
    uint8_t installed_count;
    uint8_t installed[FE2_INSTALLED_ITEMS];
    uint16_t shield_capacity;
} fe2_craft_fields;

typedef struct fe2_body_fields {
    uint16_t orbit_product[4];  /* wide orbit product, low word first */
    uint32_t orbit_radius;
    uint16_t orbit_inclination;
    uint8_t time_shift;         /* doublings of the orbit time word */
    uint8_t docking_state;
    uint8_t docking_claim;      /* handle of the craft cleared to dock */
    uint8_t generation;         /* bit 7 generated name; low bits depth */
    uint16_t basis_seed;
    uint16_t shape;             /*; highest docking slot of a station */
    uint8_t docked[8];          /* handles in the docking slots */
    uint16_t docking_timer;
    uint16_t scale_exponent;
    uint16_t radius_class;      /*; a police source's ships to launch (signed) */
    uint16_t progress;          /* a police source's launch progress */
    uint16_t temperature;
} fe2_body_fields;

/* The same bytes for a craft in flight, as the engine uses them. */
typedef struct fe2_flight_fields {
    uint16_t camera_flags;       /*, shared with the craft view */
    int16_t control_limits[3][2]; /* each control's positive then negative limit */
    uint32_t equipment;
    uint32_t equipment_damaged;
    uint8_t drive;
    uint8_t capacity_class;     /* set from the model at creation */
    uint8_t installed[12];
    uint16_t shield_charge;          /* cleared at creation */
    uint16_t shield_capacity;
} fe2_flight_fields;
#pragma pack(pop)

typedef struct fe2_object {
    int16_t orientation[9];     /* rows of the Q15 basis */
    uint8_t rebuild_budget;     /* basis steps left before it is renormalized */
    uint8_t render_state;       /* the frame  the absolute position cache is of */
    uint32_t camera_offset[3];  /* camera frames; else the cached absolute position's low dwords */
    uint32_t camera_target[3];  /* camera frames; else the cached absolute position's high dwords */
    uint32_t position_low[3];
    uint32_t position_high[3];
    uint8_t parent_handle;
    uint8_t station_slot;
    int16_t alternate_orientation[9]; /* basis used while docked */
    uint16_t model_parameter;   /* read by some models (orbit rings) */
    uint16_t model;
    uint8_t basis_generation;   /* the frame  alternate_orientation was built in */
    uint8_t handle;
    uint8_t role;
    uint8_t level;
    uint8_t update_period;      /* owner updates every period + 1 passes */
    uint8_t update_countdown;
    int32_t velocity[3];
    uint16_t screen_x, screen_y; /*, +0x70 last projected position */
    uint16_t clock;             /* camera frames only */
    uint16_t cargo_type;
    uint32_t target_seed;
    uint16_t size_mantissa;
    uint16_t size_exponent;
    uint32_t orbit[2];          /*, +0x82; a 64-bit motion sample, low first */
    uint32_t scalar;            /* vector scale, +0x88 exponent */
    int16_t spin[3];            /* a turning craft's angular remainders (the same bytes) */
    int16_t controls[3];        /* a flying craft's controls (the bytes of vector_delta[1..2]
                                 * and the views' first word) */
    uint16_t vector_delta[3];   /* (camera frames: +0x8c is camera_roll) */
    uint32_t camera_roll;       /* camera frames only */
    union {
        fe2_craft_fields craft;
        fe2_body_fields body;
        fe2_flight_fields flight;
    } as;
    uint16_t hull;
    uint16_t cooldown;          /* counts down by the frame step (flight) */
    uint16_t busy_remaining;    /* equipment busy time (third gauge source) */
    uint16_t velocity_fraction[3]; /* fractions below velocity (small-step integration) */
    int32_t feedback[3];        /* a flying craft's feedback velocity limits */
    uint32_t hyperspace[3];     /* : a hyperspace cloud's origin system first (the same bytes) */
    uint8_t behaviour_parent;   /* the parent the autopilot expects (flight) */
    uint8_t behaviour;          /* AI/autopilot state */
    uint8_t behaviour_target;   /* handle the behaviour acts on */
    uint8_t control_class;      /* 0..2 by the control limit  */
    int16_t source_offset[3];
    int16_t steering[3];        /* a feedback direction (relative words) */
    uint16_t steering_shift;
    int16_t rotation_rates[3];
    uint16_t capacity;          /* the model's capacity less the drive's mass */
    uint8_t map_marker;         /* 0xff: map marker without size */
    uint8_t drive_fuel;         /* hyperspace fuel of non-player craft */
    uint32_t reward;            /* shown with map marker 0xfb (flight) */
    uint32_t fuel;
    char name[FE2_OBJECT_NAME_BYTES]; /* (the model scales follow at +0x10a) */
    uint16_t model_scale_a[3];  /* 48-bit values from the model (scales) */
    uint16_t model_scale_b[3];  /*; a body's gravity is the first word */
    uint16_t transition_countdown;
    uint16_t flags;             /* (low byte: the model variant at creation) */
    uint8_t trail;              /* nonzero: a trail effect each motion pass (flight) */
    uint8_t effect;             /* the transient effect */
    uint8_t probe_countdown;
    uint8_t frame_state;
    uint16_t stored_model;      /* the model an event craft shows again (people) */
    /* An event craft's arrival (people): the bytes of hyperspace[1..2]. */
    uint16_t event_param;
    uint8_t resume_behaviour;   /* the behaviour after the wait */
    uint8_t event_pad;
    uint32_t event_delay;
} fe2_object;

/* Opaque collection of the current system's objects. */
typedef struct fe2_object_list fe2_object_list;

#endif

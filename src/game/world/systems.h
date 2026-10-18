/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SYSTEMS_SYSTEMS_H
#define FE2_SYSTEMS_SYSTEMS_H

/*
 * Star system generation and object instantiation (systems).
 *
 * Generation fills a run of compact system records (system_record.h): a
 * handcrafted system from its template, or a procedural system grown from
 * two random seeds through stellar/planetary profiles, recursive children,
 * root groups and profile rules. Instantiation turns a record list into
 * runtime objects. Arithmetic on normalized mantissa/exponent pairs and all
 * other game services come through `fe2_system_services`.
 */
#include "system_record.h"
#include "game/call_args.h"
#include "game/scalar.h"
#include "game/object.h"

/* Model descriptor header fields (resident 0x50). */
typedef struct fe2_model_header {
    uint16_t radius[2];   /*, +0xc */
    uint16_t scale;
} fe2_model_header;

enum { FE2_NAME_TEXT_BYTES = 32 };

typedef struct fe2_system_services {
    void *opaque;

    /* Resident math (math), as the original uses them. */
    fe2_scalar (*normalize_dword)(void *opaque, uint32_t value);
    /* Doubled signed product: high word, exponents added. */
    fe2_scalar (*multiply)(void *opaque, fe2_scalar a, fe2_scalar b);
    fe2_scalar (*divide)(void *opaque, fe2_scalar dividend, fe2_scalar divisor);
    fe2_scalar (*square_root)(void *opaque, fe2_scalar value);
    fe2_scalar (*normalize_word)(void *opaque, fe2_scalar value);
    fe2_scalar (*add)(void *opaque, fe2_scalar a, fe2_scalar b);
    uint32_t (*to_signed_dword)(void *opaque, fe2_scalar value);
    uint32_t (*to_unsigned_dword)(void *opaque, fe2_scalar value);
    /* Orientation of `object` from the two basis angles in the generation
     * state (shared resident 0x1f35/0x1f37), permutations A and B. */
    void (*basis_a)(void *opaque, fe2_object *object);
    void (*basis_b)(void *opaque, fe2_object *object);
    /* Wide orbit product from parameters 0..4: four words, low first. */
    void (*wide_product)(void *opaque, const fe2_call_args *args, uint16_t product[4]);
    void (*interpolate)(void *opaque, fe2_object *object, const fe2_call_args *args);
    /* Three signed dword components rescaled in place (parameter 6 scale). */
    void (*rescale_vector)(void *opaque, uint32_t vector[3], const fe2_call_args *args);

    /* Objects, models, names and text. */
    fe2_object *(*allocate_object)(void *opaque, const fe2_call_args *args);
    /* Records hold a 32-bit reference to their runtime object (a far
     * pointer in the original). */
    uint32_t (*object_reference)(void *opaque, const fe2_object *object);
    fe2_object *(*object_from_reference)(void *opaque, uint32_t reference);
    void (*set_object_flags)(void *opaque, fe2_object *object, uint8_t flags);
    const fe2_model_header *(*model)(void *opaque, uint16_t model);
    void (*format_table_text)(void *opaque, uint16_t selector);
    void (*generate_name)(void *opaque, uint16_t type, uint32_t seed,
                          char out[FE2_NAME_TEXT_BYTES]);                     /* names */
} fe2_system_services;

struct fe2_system_generation;
/* A profile rule works on the new record; it may append stations after it
 * and leaves *record at the last record it wrote. */
typedef void (*fe2_profile_rule)(struct fe2_system_generation *generation, unsigned *record);

/*
 * Values the generation routines share. The original keeps them in resident
 * memory (scratch 0x7bc..0x7e9 and 0x2021..0x204e) and saves/restores them at
 * the same points the port does.
 */
typedef struct fe2_system_generation {
    const fe2_system_services *game;
    fe2_record_space space;          /* the destination segment */

    /* Scratch shared by profile, child and root-group generation. */
    uint32_t value;                  /* size of the body being created */
    uint32_t position_u;             /*; low word scales the scalar */
    uint32_t delta;
    uint32_t limit;
    uint32_t scale;
    uint16_t step;
    uint16_t curve;
    uint16_t position;               /* curve position */
    unsigned destination;            /* next record */
    unsigned parent;                 /* parent record */
    uint16_t subtree_position;
    fe2_profile_rule rule;
    uint32_t name_cursor;            /* byte position after the last name written */

    /* Generation counters and parameters. */
    uint32_t random_a, random_b;
    const char *root_name;
    uint16_t record_count;
    uint8_t named_bodies;
    uint8_t root_letter;
    uint8_t moon_digit;
    uint8_t moon_letter;
    uint8_t sub_digit;
    uint8_t depth;
    uint16_t created_index;
    uint16_t parent_index;           /* one-based */
    uint8_t root_kind;
    uint8_t root_budget;
    uint32_t root_size;
    unsigned base;                   /* first record of the list */
    uint16_t mode;
    uint32_t key;
    uint16_t danger;
    uint16_t published_count;
    uint8_t station_count;
    uint8_t sol_station_count;
    uint16_t basis_angle[2];         /* shared with the math service */
} fe2_system_generation;

/* Vector 0x8a inputs (registers and argument block). */
typedef struct fe2_system_request {
    uint32_t seed_a;          /* DX:AX; high word zero selects a handcrafted system */
    uint32_t seed_b;          /* CX:BX; root size */
    uint16_t mode;            /* zero generates, otherwise instantiates */
    uint32_t limit;           /* dword; low byte is the root kind */
    uint32_t key;             /* handcrafted system key */
    uint8_t danger;
    unsigned destination;     /* first record index */
    unsigned parent;          /* parent record index */
    const char *root_name;
    uint32_t orbit[2];        /* orbit inputs for instantiation */
    /* The original's DI on entry; only a root record finalized before any
     * name is written reads through it. */
    uint16_t name_cursor;
} fe2_system_request;

/* 0x8a: generate a system into `space` or instantiate its records. */
void fe2_systems_action(fe2_system_generation *generation, const fe2_system_services *game,
                        fe2_record_space space, const fe2_system_request *request);

/* 0x92: format systems text `selector` through the resident text formatter. */
void fe2_systems_format_text(const fe2_system_services *game, uint16_t selector);

#endif

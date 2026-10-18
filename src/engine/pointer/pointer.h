/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_FRONTIER_POINTER_H
#define FE2_FRONTIER_POINTER_H

/*
 * The pointer hit-region list (image 0xd908..0xd9f4, services 0x68..0x7c).
 * Overlays publish rectangles with an ID; the pointer service hit-tests
 * them. The list is terminated by a record whose left edge is 0xffff.
 *
 * The original has room for 128 records and no capacity check (appends past
 * it run into the function-key template). The port refuses an append that
 * does not fit and leaves the list unchanged.
 */
#include <stddef.h>
#include <stdint.h>

enum { FE2_POINTER_REGIONS = 128, FE2_POINTER_REGION_END = 0xffff,
       FE2_POINTER_FUNCTION_KEY_REGIONS = 16, FE2_POINTER_MISSILE_REGIONS = 5 };

typedef struct fe2_pointer_region {
    uint16_t left, top, right, bottom, id;
} fe2_pointer_region;

/* The live list (0x1ea9:0xd5): records up to the terminator. The record
 * after the last slot only ever holds the terminator. */
typedef struct fe2_pointer_regions {
    fe2_pointer_region region[FE2_POINTER_REGIONS + 1];
} fe2_pointer_regions;

/* The function-key template (0x1ea9:0x5d7) and the missile buttons
 * (0x1ea9:0x43), generated from the resources. */
extern const fe2_pointer_region *fe2_pointer_function_key_regions;
extern const fe2_pointer_region *fe2_pointer_missile_regions;
int fe2_pointer_bind(const uint8_t *image, size_t count);

/* The initial image: every word 0xffff. */
void fe2_pointer_regions_init(fe2_pointer_regions *list);
/* The number of records before the terminator. */
size_t fe2_pointer_regions_count(const fe2_pointer_regions *list);
/* empty the list. */
void fe2_pointer_regions_clear(fe2_pointer_regions *list);
/* the function-key template only. */
void fe2_pointer_regions_reset(fe2_pointer_regions *list);
/* append a stream of records: the first unconditionally, then each
 * until one whose left edge is 0xffff (or `available` records). Returns the
 * records appended, or -1 when they do not fit. */
int fe2_pointer_regions_append(fe2_pointer_regions *list, const fe2_pointer_region *stream, size_t available);
/* a rectangle `x_extent` and `y_extent` about (x, y), its
 * minima clamped to zero when negative (as signed words), its maxima
 * wrapping. Returns 0, or -1 when the list is full. */
int fe2_pointer_regions_add(fe2_pointer_regions *list, uint16_t x, uint16_t y, uint16_t x_extent,
                            uint16_t y_extent, uint16_t id);
/* remove the first record with `id`, shifting the rest down. The
 * search and the shift stop at any negative left edge (not only 0xffff). */
void fe2_pointer_regions_remove(fe2_pointer_regions *list, uint16_t id);


/* ---- pointer.c: 0xd223..0xd34b ----------------------------------------- */

/* The pointer's shared input (the main data segment). */
typedef struct fe2_pointer_state {
    uint16_t x, y;                   /* the drawn position (the redraw publishes it) */
    uint8_t primary, secondary;      /* 0xff while held */
    uint8_t primary_latch, secondary_latch; /* a press already consumed */
    int16_t redirected[2];           /* motion while the secondary is held */
    uint8_t gameport_flags;          /* bit 5 redirects gameport motion */
    int16_t gameport[2];             /* gameport motion */
    uint16_t pressed[2];             /* the position last tested */
    uint16_t hold;                   /* subticks the primary is held (1 at a press) */
    uint8_t input_code;              /* the region ID published to the overlays */
    uint8_t region[2];               /* the region of the last press, the one before */
    uint8_t age[2];                  /* subticks since the last press, at the last press */
} fe2_pointer_state;

/* The host's sample of the mouse, gameport and keyboard : combined
 * motion and the button states (0x0 or 0xff). */
typedef struct fe2_pointer_sample {
    int16_t motion[2];
    uint8_t primary, secondary;
} fe2_pointer_sample;

/* (0xd15a's end): publish the buttons; a release rearms its latch. */
void fe2_pointer_publish_buttons(fe2_pointer_state *pointer, uint8_t primary, uint8_t secondary);
/* nonzero for a press not yet consumed (it is then consumed). */
int fe2_pointer_consume_primary(fe2_pointer_state *pointer);
int fe2_pointer_consume_secondary(fe2_pointer_state *pointer);
/* (service 0x4c): sample, then move the pointer. With the secondary
 * held (or gameport motion and flag bit 5) the motion goes to `redirected`
 * and the position stays. Otherwise it is clamped to 0..319, 0..198. With
 * the primary held the regions are hit-tested (inclusive, signed; the last
 * match wins, 0xff for none, the ID's low byte). A new press publishes the
 * hit, records the region and age history and starts the hold count; a
 * hold of 26 subticks republishes the hit every query. Returns the new
 * position in *x, *y (the pointer's x, y are not updated). */
void fe2_pointer_query(fe2_pointer_state *pointer, const fe2_pointer_regions *regions,
                       const fe2_pointer_sample *sample, uint16_t *x, uint16_t *y);

#endif

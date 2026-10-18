/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/*
 * 0x130f9..0x135df the interval list of a geometry walk.
 *
 * The list lives in the walk's workspace (0x97e bytes of stack): node n at
 * +0x1e + 12n, a bump cursor at +0x1c. Nodes are never freed, and the cursor
 * is not checked (FE2_PROBE_NODES fill the workspace). Positions compare
 * unsigned. A node a covers [position(a), position(next(a))). Kinds with
 * bit 1 (2: the initial node 0, 7) are open; kinds 7, 0xd and 0x15 (the
 * end) close the list for the hit search.
 */
#include <string.h>

#include "scene_probe_internal.h"

enum {
    OPEN = 0x02, KIND_PAINTED = 0x08, KIND_END = 0x0d, KIND_CLOSED = 0x07,
    STATE_FIRST = 0x1f02, STATE_LAST = 0x1d15, STATE_OUTSIDE = 0x1f10,
    END_NODE = 1, END_POSITION = 0xffff
};

#define POS(n) (list->node[n].position)
#define NEXT(n) (list->node[n].next)
#define STATE(n) (list->node[n].state)

void fe2_probe_intervals_init(fe2_probe_intervals *list)
{
    memset(list, 0, sizeof *list);
    list->node[0].position = 0;
    list->node[0].next = END_NODE;
    list->node[0].state = STATE_FIRST;
    list->node[END_NODE].position = END_POSITION;
    list->node[END_NODE].next = 0;
    list->node[END_NODE].state = STATE_LAST;
    list->count = 2;
}

/* A new node (unused fields zero; the original's hold whatever the
 * workspace held). Past the workspace the original overwrites its caller's
 * stack; the port reuses the last node. */
static uint16_t take(fe2_probe_intervals *list)
{
    uint16_t n = list->count;
    if (n < FE2_PROBE_NODES) ++list->count;
    else n = FE2_PROBE_NODES - 1;
    return n;
}

/* A node inserted after `a`. */
static uint16_t insert_after(fe2_probe_intervals *list, uint16_t a, uint16_t position)
{
    uint16_t f = take(list);
    NEXT(f) = NEXT(a);
    NEXT(a) = f;
    POS(f) = position;
    return f;
}

static int open_node(const fe2_probe_intervals *list, uint16_t n) { return (list->node[n].state & OPEN) != 0; }

static void set_vector(fe2_probe_intervals *list, uint16_t n, const int16_t v[3])
{
    memcpy(list->node[n].vector, v, sizeof list->node[n].vector);
}

static uint16_t tagged(uint8_t tag, uint8_t kind) { return (uint16_t)(tag << 8 | kind); }

/* The node holding `low`: position(a) <= low < position(next(a)). */
static uint16_t holding(const fe2_probe_intervals *list, uint16_t low)
{
    uint16_t a = 0;
    while (low >= list->node[list->node[a].next].position) a = list->node[a].next;
    return a;
}

void fe2_probe_paint_open(fe2_probe_intervals *list, uint16_t low, uint16_t high, const int16_t v[3], uint8_t tag)
{
    uint16_t a = holding(list, low), b = NEXT(a), f;
    if (open_node(list, a)) {
        if (high <= POS(b)) {
            if (high != POS(b)) {
                f = insert_after(list, a, high);
                STATE(f) = STATE(a);
                b = f;
            }
            if (low != POS(a)) a = insert_after(list, a, low);
            STATE(a) = tagged(tag, KIND_PAINTED);
            set_vector(list, b, v);
            return;
        }
        /* The range runs past b: drop the open boundaries it covers. */
        for (;;) {
            if (!open_node(list, b)) {
                f = insert_after(list, a, low);
                STATE(f) = tagged(tag, KIND_PAINTED);
                b = NEXT(b);
                goto beyond;
            }
            f = NEXT(b);
            if (high <= POS(f)) break;
            NEXT(a) = f;
            b = f;
        }
        if (high != POS(f)) {
            uint16_t g = insert_after(list, b, high);
            STATE(g) = STATE(b);
            f = g;
        }
        POS(b) = low;
        STATE(b) = tagged(tag, KIND_PAINTED);
        set_vector(list, f, v);
        return;
    }
beyond:
    for (;;) {
        uint16_t c;
        if (high <= POS(b)) return;
        if (!open_node(list, b)) {
            b = NEXT(b);
            continue;
        }
        c = NEXT(b);
        if (high <= POS(c)) {
            if (high != POS(c)) {
                f = insert_after(list, b, high);
                STATE(f) = STATE(b);
                c = f;
            }
            STATE(b) = tagged(tag, KIND_PAINTED);
            set_vector(list, c, v);
            return;
        }
        STATE(b) = tagged(tag, KIND_PAINTED);
        for (;;) {
            if (!open_node(list, c)) {
                b = NEXT(c);
                break;
            }
            f = NEXT(c);
            if (high <= POS(f)) {
                if (high == POS(f)) {
                    NEXT(b) = f;
                    c = f;
                }
                POS(c) = high;
                set_vector(list, c, v);
                return;
            }
            NEXT(b) = f;
            c = f;
        }
    }
}

void fe2_probe_paint_end(fe2_probe_intervals *list, uint16_t low, uint16_t high, const int16_t v[3], uint8_t tag)
{
    uint16_t a = holding(list, low), b = NEXT(a);
    if (open_node(list, a)) {
        if (low != POS(a)) {
            uint16_t f = take(list);
            NEXT(a) = f;
            POS(f) = low;
            a = f;
        }
        set_vector(list, a, v);
        STATE(a) = tagged(tag, KIND_END);
        NEXT(a) = END_NODE;
        return;
    }
    for (;;) {
        if (high <= POS(b)) return;
        if (open_node(list, b)) break;
        b = NEXT(b);
    }
    STATE(b) = tagged(tag, KIND_PAINTED);
    NEXT(b) = END_NODE;
}

void fe2_probe_paint_closed(fe2_probe_intervals *list, uint16_t low, uint16_t high, const int16_t v[3],
                            uint8_t tag)
{
    uint16_t a = 0, b;
find_low:
    /* Open nodes ending at or before `low` are closed off. */
    if (open_node(list, a)) {
        b = NEXT(a);
        if (low < POS(b)) {
            if (low != POS(a)) a = insert_after(list, a, low);
            set_vector(list, a, v);
            goto close;
        }
        STATE(a) = STATE_OUTSIDE;
        a = b;
        goto find_low;
    }
    a = NEXT(a);
    if (low >= POS(a)) goto find_low;
    if (high <= POS(a)) goto outside;
    if (open_node(list, a)) goto upper;
next_open:
    a = NEXT(a);
    if (high <= POS(a)) goto outside;
    if (!open_node(list, a)) goto next_open;
upper:
    b = NEXT(a);
close:
    STATE(a) = tagged(tag, KIND_CLOSED);
    /* The open boundaries inside the range are dropped. */
    while (high > POS(b)) {
        if (!open_node(list, b)) goto next_open;
        b = NEXT(b);
        NEXT(a) = b;
    }
    if (high != POS(b)) STATE(insert_after(list, a, high)) = STATE_OUTSIDE;
    a = b;
outside:
    /* Open nodes from here on (but the last) are closed off. */
    b = NEXT(a);
    while (b != 0) {
        if (open_node(list, a)) STATE(a) = STATE_OUTSIDE;
        a = b;
        b = NEXT(a);
    }
}

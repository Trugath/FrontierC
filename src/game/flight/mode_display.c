/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* The cockpit mode display, the command widgets and the ship command
 *. */
#include "flight_internal.h"

enum { PANEL_Y = 0xbc, SPRITE_RANGE = 0x48 };

static void sprite(flight_context *c, uint16_t sprite_id, uint16_t x, uint16_t y)
{
    c->g->draw_sprite(c->g->opaque, sprite_id, x, y);
}

/*
 * 0x3de5: while the cockpit is shown, the view label (panels request 4,
 * text 0x861a + view), the panel sprites for the annotate and mode-four
 * flags, the jump range sprite (when a jump is set up, in modes up to 0xc
 * and far out, drives accepting it), the view sprite and the flight mode
 * sprite.
 *
 * Returns the accumulator as the original leaves it: one caller keeps
 * using EAX as its step. The view label leaves AX = 4 (and the range query
 * EAX = the jump distance's high word : 1); the view sprite's selection
 * leaves AL = the view index. The services are taken to keep AX.
 */
uint32_t flight_refresh_mode_display(flight_context *c, uint32_t eax)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_object_list *saved = s->current_objects;
    fe2_flight_drive_reply reply;
    fe2_call_args args;
    uint16_t id;
    uint8_t mode;
    if (!cockpit_shown(s)) return eax;
    fe2_args_clear(&args);
    fe2_args_word(&args, P2, (uint16_t)(s->view_index + 0x861a));
    g->panels_panel(g->opaque, 4, &args);
    eax = (eax & 0xffff0000u) | 4;
    sprite(c, 0x2c, 0x100, PANEL_Y);
    if (s->control_flags & FE2_CONTROL_ANNOTATE) sprite(c, 0x2d, 0x130, PANEL_Y);
    if (s->control_flags & FE2_CONTROL_MODE_FOUR) sprite(c, 0x32, 0x120, PANEL_Y);
    if (s->jump_enabled != 0 && s->mode <= FE2_FLIGHT_RELATIVE && (s->motion_flags & 0x40)) {
        fe2_args_clear(&args);
        fe2_args_dword(&args, P1, s->jump_distance);
        fe2_args_object(&args, P_SUBJECT, s->ship);
        c->subject = s->ship;
        g->hyperspace_query(g->opaque, 1, &args, &reply);
        eax = (s->jump_distance & 0xffff0000u) | 1;
        if ((int16_t)reply.mode >= 0) sprite(c, SPRITE_RANGE, 0x110, PANEL_Y);
    }
    switch (s->view_index) {
    case 0: id = 0x33; break;
    case 1: id = 0x34; break;
    case 4: id = 0x35; break;
    default: id = 0x4a; break;
    }
    sprite(c, id, 0, PANEL_Y);
    eax = (eax & 0xffffff00u) | s->view_index;
    mode = s->mode;
    if (mode != FE2_FLIGHT_AUTOPILOT && mode != FE2_FLIGHT_DISTANCE) {
        if (mode < FE2_FLIGHT_ENTER_LANDED) {
            id = 0x2f;
            if (s->control_flags & FE2_CONTROL_THRUST) {
                id = 0x2e;
                if (s->target == 0 || !bit(s->ship->as.flight.equipment, 21)) id = 0;
            }
        } else if (mode < FE2_FLIGHT_ENTER_DOCKED) {
            id = 0x30;
        } else {
            id = s->mining_mode_active != 0 ? 0x31 : 0x49;
        }
        if (id != 0) sprite(c, id, 0x100, PANEL_Y);
    }
    s->current_objects = saved;
    return eax;
}

void fe2_flight_refresh_mode(fe2_flight_state *state, const fe2_flight_services *game)
{
    flight_context c;
    c.s = state;
    c.g = game;
    c.subject = 0;
    flight_refresh_mode_display(&c, 0);
}


void flight_disable_widgets(flight_context *c)
{
    c->s->control_flags &= (uint8_t)~FE2_CONTROL_WIDGETS;
    c->g->panels_widgets(c->g->opaque, 0, 3, 0);
    flight_clear_regions(c);
}

/* widgets that were shown are redrawn; otherwise removed. */
void flight_clear_widget_flag(flight_context *c)
{
    if (c->s->control_flags & FE2_CONTROL_WIDGETS) {
        c->s->control_flags &= (uint8_t)~FE2_CONTROL_WIDGETS;
        flight_refresh_widgets(c);
    } else {
        flight_disable_widgets(c);
    }
}


void flight_widgets_if_enabled(flight_context *c)
{
    if (c->s->control_flags & FE2_CONTROL_WIDGETS) flight_refresh_widgets(c);
}

/*
 * 0x3f55: panels request 1 (widgets 3), then the eight equipment slots
 * (+0xac..+0xb3, last first): each fitted slot's sprite (by its kind's
 * low three bits and the slot's parity) at its layout position, first
 * registered as a pointer region (key 0x10 + slot) when the widgets were
 * not shown. Then the pending slot's label, the bottom row sprites by
 * equipment bits and modes, and the widgets' rectangles when they were
 * not shown.
 */
void flight_refresh_widgets(flight_context *c)
{
    fe2_flight_state *s = c->s;
    const fe2_flight_services *g = c->g;
    fe2_object *ship;
    fe2_call_args args;
    uint32_t bits;
    uint16_t x, y, text;
    uint8_t kind;
    int i;
    static const struct { uint8_t bit; uint16_t sprite, x; } row[] = {
        {6, 0x45, 0xb0}, {0xb, 0x44, 0x70}, {0xa, 0x46, 0xd0}, {8, 0x47, 0x60}, {0x12, 0x4b, 0xc0}};
    unsigned k;
    fe2_args_clear(&args);
    fe2_args_word(&args, P2, 1);
    g->panels_widgets(g->opaque, 1, 3, &args);
    ship = s->ship;
    for (i = 7; i >= 0; --i) {
        kind = ship->as.flight.installed[4 + i];
        if (kind == 0) continue;
        x = fe2_flight_equipment_slot_centres[i][0];
        y = fe2_flight_equipment_slot_centres[i][1];
        if (!(s->control_flags & FE2_CONTROL_WIDGETS))
            g->add_region(g->opaque, x, y, 7, (uint16_t)(i + 0x10), &x, &y);
        sprite(c, fe2_flight_weapon_sprites[kind & 7][i & 1], (uint16_t)(x - 8), (uint16_t)(y - 7));
    }
    text = 0x8628;
    if (s->pending_action != 0) {
        kind = ship->as.flight.installed[3 + s->pending_action];
        if (kind != 0) text = (uint16_t)((kind & 7) + 0x8629);
        else s->pending_action = 0;
    }
    fe2_args_clear(&args);
    fe2_args_word(&args, P1, text);
    fe2_args_word(&args, P_MODE, 0x64);
    g->draw_text_both(g->opaque, 0x8627, s->widget_colour, 0x78, 0xac, &args);
    sprite(c, 0x40, 0x60, 0xba);
    if (s->pending_action != 0) sprite(c, 0x41, 0x90, PANEL_Y);
    /* The equipment dword with its halves swapped (as the original reads
     * it at 0x7d8). */
    bits = ship->as.flight.equipment << 16 | ship->as.flight.equipment >> 16;
    if (bit(bits, 7)) sprite(c, 0x43, 0x80, PANEL_Y);
    else if (bit(bits, 3)) sprite(c, 0x42, 0x80, PANEL_Y);
    for (k = 0; k < sizeof row / sizeof row[0]; ++k)
        if (bit(bits, row[k].bit)) sprite(c, row[k].sprite, row[k].x, PANEL_Y);
    if (s->free_mining_machine_count != 0 && s->mode == FE2_FLIGHT_MINING) sprite(c, 0x4c, 0xc0, PANEL_Y);
    if (s->motion_flags & 0x10) sprite(c, 0x4d, 0xc0, PANEL_Y);
    if (s->control_flags & FE2_CONTROL_WIDGETS) return;
    s->control_flags |= FE2_CONTROL_WIDGETS;
    g->set_widgets(g->opaque, fe2_flight_command_rectangles);
}

/*
 * 0x4159 (key 0x64): motion flag 0x10 asks mining for command 1, mode 0x68 with
 * 0x5191 set for command 0. Otherwise a ship with equipment bit 2 marks
 * the mission records (kinds 3 and 4) of the current system whose object
 * is of level 0xd or below, keeping their lowest level; then effect 0x27
 * and the result message.
 */
void flight_ship_command(flight_context *c, fe2_object *ship)
{
    fe2_flight_state *s = c->s;
    fe2_call_args args;
    fe2_flight_record *r;
    uint16_t n;
    uint8_t kind, level;
    if ((s->motion_flags & 0x10) || (s->free_mining_machine_count != 0 && s->mode == FE2_FLIGHT_MINING)) {
        fe2_args_clear(&args);
        fe2_args_object(&args, P_SUBJECT, ship);
        c->g->mining_command(c->g->opaque, (s->motion_flags & 0x10) ? 1 : 0, &args);
        return;
    }
    if (!bit(ship->as.flight.equipment, 2)) return;
    if ((int16_t)(s->record_count - 1) >= 0) {
        for (n = 0, r = s->records; n < s->record_count; ++n, ++r) {
            kind = r->kind & 0xf;
            if ((kind != 3 && kind != 4) || r->system != s->current_system || r->handle == 0) continue;
            s->current_objects = s->objects;
            level = flight_lookup(c, r->handle)->level;
            if (level > 0xd) continue;
            r->seen = 0xff;
            if (level <= r->lowest_level) r->lowest_level = level;
        }
    }
    flight_effect(c, 0x27);
    flight_message(c, TEXT_COMMAND_RESULT, 0);
}

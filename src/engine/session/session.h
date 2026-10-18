/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#ifndef FE2_SESSION_H
#define FE2_SESSION_H

#include "pool/pool.h"
#include "objects/objects.h"
#include "pointer/pointer.h"
#include "input/input.h"
#include "resident.h"
#include "overlay_calls.h"
#include <stddef.h>

enum { FE2_SESSION_OVERLAYS = 64, FE2_SESSION_DEFAULT_LAST = 17 };
typedef struct fe2_session fe2_session;
typedef struct fe2_session_metadata {
    uint8_t activation, flags, update, draw;
    uint32_t events;
} fe2_session_metadata;

/* Native overlay entries. Callbacks may change current_overlay/last_overlay,
 * metadata, scheduling and game state, as the resident services permit. */
typedef struct fe2_session_overlay {
    void *context;
    fe2_session_metadata metadata;
    void (*initialize)(void *context, fe2_session *session, uint16_t selection);
    void (*activate)(void *context, fe2_session *session, uint16_t input);
    uint32_t (*event)(void *context, fe2_session *session, uint32_t event);
    void (*update)(void *context, fe2_session *session);
    void (*draw)(void *context, fe2_session *session);
    void (*coarse_tick)(void *context, fe2_session *session, uint32_t interval);
    void (*periodic_record)(void *context, fe2_session *session, fe2_object *record, uint64_t time);
    void (*second_record)(void *context, fe2_session *session, fe2_object *record, uint32_t interval);
    /* : a resident message (the in-flight message and panel slots). */
    void (*message)(void *context, fe2_session *session, const fe2_objects_message *message);
    /* : the overlay selection moved away from this slot. */
    void (*deselect)(void *context, fe2_session *session);
    /*, for metadata flag bit 2: a record is released (request) or
     * damaged (request 0xffff) before the damage is applied. */
    void (*record_event)(void *context, fe2_session *session, fe2_object *record, uint32_t request);
} fe2_session_overlay;

typedef struct fe2_session_services {
    void *context;
    void (*reset_resources)(void *context);
    void (*synchronize_pointer)(void *context, fe2_session *session);
    void (*begin_frame)(void *context, fe2_session *session);
    /* Zero means no input. Unmatched input is restored to the host latch. */
    uint16_t (*poll_input)(void *context);
    void (*restore_input)(void *context, uint16_t input);
    void (*activation_sound)(void *context);
    void (*assign_palette)(void *context, fe2_session *session);
    void (*restore_color)(void *context, fe2_session *session);
    void (*execute_frame)(void *context, fe2_session *session);
    void (*present)(void *context, fe2_session *session);
    /* Pool providers: 0x18b5 model descriptors, 0x344 level sounds and 0x188
     * record distances. */
    const uint8_t *(*model)(void *context, uint16_t model);
    void (*play_level)(void *context, uint16_t effect, uint8_t level);
    int16_t (*distance)(void *context, const fe2_object *from, const fe2_object *to);
    /* Restart music: 0x354 reset, 0x350 stop effects, 0x338 select (index >= 0). */
    void (*reset_music)(void *context);
    void (*stop_effects)(void *context);
    void (*select_music)(void *context, int16_t index);
    /* the mouse, gameport and keyboard motion and buttons. */
    void (*sample_pointer)(void *context, fe2_session *session, fe2_pointer_sample *sample);
    /* the screenshot input code (0xfd) was polled. */
    void (*screenshot)(void *context, fe2_session *session);
    /* The intro and exit scene: the builtin model and text banks, the
     * drawing color (0x144 with BX, CL; 0x15c restores it), clearing the
     * screen, the music setting check, clearing the view
     *  and rendering a record in its explicit frame. */
    void (*restore_banks)(void *context, fe2_session *session);
    void (*set_drawing_color)(void *context, uint16_t bx, uint8_t cl);
    void (*restore_drawing_color)(void *context);
    void (*clear_screen)(void *context);
    void (*check_music)(void *context);
    void (*clear_view)(void *context);
    void (*render_record)(void *context, fe2_session *session, fe2_object *record);
} fe2_session_services;

/* Record damage from the pool's nearby effect (bound by the objects layer). */
typedef void (*fe2_session_damage)(void *context, fe2_session *session, fe2_object *record, uint16_t amount,
                                   uint16_t attacker);

struct fe2_session {
    fe2_session_services services;
    fe2_session_overlay overlays[FE2_SESSION_OVERLAYS];
    fe2_session_metadata metadata[FE2_SESSION_OVERLAYS];
    uint32_t event_masks[FE2_SESSION_OVERLAYS];
    uint8_t loaded[FE2_SESSION_OVERLAYS], locks[FE2_SESSION_OVERLAYS];
    unsigned selected_overlay, current_overlay, last_overlay;
    fe2_pool_state pools;
    fe2_object_pool objects;
    fe2_object records[FE2_POOL_SLOTS];
    /* The second pool : the map's and station's generated systems. */
    fe2_object_pool map_objects;
    fe2_object map_records[FE2_POOL_SLOTS];
    uint32_t pending_simulation, pending_render, simulation_step, render_step;
    uint32_t time_step, coarse_time, noise[2];
    uint32_t event_time;               /* advanced with the render time */
    uint64_t scheduled_time;
    uint64_t timer_phase; /* host microseconds -> original PIT divisor 5899 */
    uint16_t frame_ticks;
    uint8_t subtick_countdown, generation, render_mode;
    uint16_t panel_selection;          /* the overlay owning the selected panel */
    uint8_t next_page;                 /* the page the next draw shows */
    int16_t current_music;
    fe2_session_damage damage;
    void *damage_context;
    /* Input: the pointer regions (services 0x68..0x7c), the pointer's shared
     * input with the input code, and the keyboard and tick state. */
    /* The overlays' shared globals the session does not own otherwise. */
    fe2_resident resident;
    /* The overlays' direct entries into each other (overlay_calls.h). */
    fe2_overlay_calls calls;
    fe2_pointer_regions regions;
    fe2_pointer_state pointer;
    fe2_input_state input;
    int pointer_shown;                 /* bit 1: the pointer is drawn and published */
    int16_t detail;                    /* renderer detail (at least 0 in the intro and exit scene) */
    uint8_t input_mask;                /* the intro's key mask (zero ignores keys) */
    uint8_t music_off;                 /* nonzero skips the intro music */
    uint16_t light[3];                 /* set by the exit scene */
    uint8_t drawing_flag;              /* 0xff after 0x158, 0 after clearing the screen  */
    /* The frontend: the intro, the selection waiting for Space's release,
     * or the exit scene with its record and distance. */
    int frontend;
    uint16_t selection, exit_mode;
    int16_t saved_detail;
    int32_t exit_distance;
    fe2_object exit_record;
    char commander[FE2_OBJECT_NAME_BYTES]; /* the exit scene's record name */
    int initialized, busy, error, restart, exit_requested;
};

typedef enum fe2_session_result {
    FE2_SESSION_ERROR = -1, FE2_SESSION_WAITING = 0, FE2_SESSION_FRAME = 1, FE2_SESSION_RESTARTED = 2,
    FE2_SESSION_EXIT = 3, /* Ctrl+C: exit to the host */
    FE2_SESSION_STARTED = 4 /* the intro's selection started the session (`selection`) */
} fe2_session_result;
enum { FE2_SESSION_PLAYING = 0, FE2_SESSION_INTRO = 1, FE2_SESSION_SELECTED = 2, FE2_SESSION_EXIT_SCENE = 3 };

/* Creates an empty native pool, binds all record handles, and installs the
 * original seeds, two-tick frame pacing and four-tick time subtick. */
/* Session storage must stay at the same address after creation. */
void fe2_session_create(fe2_session *session, const fe2_session_services *services);
/* Startup phase zero visits slots 0..17 by default. Session startup takes
 * the original selection word (low byte 1..4), runs remaining passes through
 * phase four, preserves its high byte, and resets seeds only below 0x400. */
int fe2_session_startup(fe2_session *session);
int fe2_session_start(fe2_session *session, uint16_t selection);
uint64_t fe2_session_time(const fe2_session *session);
void fe2_session_set_time(fe2_session *session, uint64_t time);
/* Serialized host events: timer threads must enqueue ticks for the owner
 * thread, rather than racing a frame. Ticks arriving in callbacks remain
 * pending according to the original capture ordering. Every fourth tick
 * runs the input tick (fe2_input_tick) before its time; Ctrl+C stops the
 * ticks and the next step returns FE2_SESSION_EXIT. */
void fe2_session_ticks(fe2_session *session, uint32_t ticks);
void fe2_session_microseconds(fe2_session *session, uint32_t elapsed);
int fe2_session_broadcast(fe2_session *session, uint32_t event);
/* take the next input code (the host's poll_input, or the native
 * input code with screenshots); 0 for none. */
uint16_t fe2_session_poll(fe2_session *session);
/* IRQ 1: one byte from the keyboard (fe2_input_scancode). */
void fe2_session_key(fe2_session *session, uint8_t byte);
/* at presentation: sample and query the pointer against the regions;
 * when the pointer is shown, its position is published.
 * Returns the queried position. A full-render step calls it before
 * `present`; incremental drawing that presents calls it itself. */
void fe2_session_update_pointer(fe2_session *session, uint16_t *x, uint16_t *y);
fe2_session_result fe2_session_step(fe2_session *session);
/* restart the session. Frame timing, the game flags, the panel
 * selection, the next page, the scheduled time's high word and the overlay
 * locks are cleared; event 3 is broadcast, the music is reset (and the
 * current music selected again), then overlay 3 is activated with the
 * current music and the frame loop resumes. The original discards the
 * stack: from inside a callback the restart is deferred, the remaining
 * callbacks of the frame (or initialization) are skipped, and the step
 * returns FE2_SESSION_RESTARTED. Callers should return straight away. */
int fe2_session_restart(fe2_session *session);
/* begin the intro (the intro's state, then frames through
 * fe2_session_step until a selection starts the session). Keys '1'..'5'
 * select 0x1, 0x101, 0x201, 0x501, 0x401; another key selects 0x1
 * once the intro time reaches 0x9c2334, and before that skips to it. The
 * selection waits for Space's release, then fe2_session_start runs and
 * the step returns FE2_SESSION_STARTED. Overlay 8's entry +0x20 (message,
 * with no message) updates the intro and +0x14 (draw) draws it. */
int fe2_session_intro(fe2_session *session);
/* (service 0x40): end the session. Mode 0 returns to the intro;
 * another mode shows the exit scene first: the commander's ship (model 6)
 * moving away until a key or the scene's time runs out. From a callback it
 * is deferred like a restart. */
int fe2_session_exit_scene(fe2_session *session, uint16_t mode);
/* The pool services of the session: owner overlays, the host providers and
 * the bound damage. */
void fe2_session_pool_services(fe2_session *session, fe2_pool_services *services);
#endif

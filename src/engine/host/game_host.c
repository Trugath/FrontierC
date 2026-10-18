/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#include "game_host.h"
#include "gameplay.h"
#include "math/math3.h"
#include "game/object_data.h"
#include <string.h>
#include <stdio.h>

enum { RECORD_MODEL = 0x5a, RECORD_OFFSET = 0x14, RECORD_VARIABLES = 0x72, RECORD_BYTES = 0x11e,
       DESCRIPTOR_EXPONENT = 0x0a, MATERIAL = 0x111, TEXT_BYTES = 256 };

static uint16_t word_at(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t dword_at(const uint8_t *p) { return (uint32_t)word_at(p) | (uint32_t)word_at(p + 2) << 16; }
static fe2_video *video(fe2_game_host *h) { return fe2_render_runtime_video(h->runtime); }

/* ---- Colour services  -------------------------------------- */
static void palette_upload(void *context, uint8_t index, uint8_t red, uint8_t green, uint8_t blue)
{
    fe2_video_palette(context, index, red, green, blue);
}
static void colour_publish(void *context)
{
    fe2_game_host *h = context;
    (void)fe2_render_update_palette_127(&h->colour, palette_upload, video(h));
}
static void colour_clear(void *context, uint16_t left, uint16_t right, uint8_t top, uint8_t bottom, uint8_t colour)
{
    fe2_game_host *h = context;
    (void)fe2_video_rectangle(video(h), left, right, top, bottom, colour, 0);
}
static void colour_restoring(void *context)
{
    fe2_video_clear_view(video(context), 0x7f);
}

void fe2_game_host_init(fe2_game_host *h, fe2_render_runtime *runtime, fe2_render_runtime_resources *resources,
                        fe2_video_present_fn present, void *present_context)
{
    unsigned i;
    memset(h, 0, sizeof(*h));
    h->runtime = runtime; h->resources = resources; h->present = present; h->present_context = present_context;
    for (i = 0; i < 256; ++i) h->colour_map[i] = (uint8_t)i;
    h->colour_services.context = h; h->colour_services.publish = colour_publish;
    h->colour_services.clear = colour_clear;
    h->colour_services.restoring = colour_restoring;
}
void fe2_game_host_attach(fe2_game_host *h, fe2_session *s)
{
    h->session = s;
    if (h->runtime && s) fe2_render_runtime_bind_noise(h->runtime, s->noise);
}

/* ---- Session services ----------------------------------------------------- */
static void restore_module(fe2_game_host *h)
{
    if (!h->module_pending_release) return;
    if (h->module_saved) {
        h->resources->text.module = h->saved_module;
        h->module_saved = 0;
    }
    h->module_pending_release = 0;
}
static void begin_frame(void *context, fe2_session *s)
{
    fe2_game_host *h = context;
    restore_module(h);
    if (h->in_frame) return;
    /* 0x7f is the viewport clear (clear_viewport_index_127). Its DAC entry is
     * the atmosphere colour, published when the frame is drawn. */
    if (fe2_render_runtime_begin(h->runtime, 0x7f)) { fprintf(stderr, "Renderer begin failed\n"); s->error = 1; ++h->errors; return; }
    h->in_frame = 1;
    /* Created detail is zero, so a part's extent is
     * shifted one or two bits less than the highest preset and stays culled
     * until the camera is about two to four times closer. */
    if (h->gameplay) {
        fe2_gameplay *play = h->gameplay;
        fe2_render_runtime_set_detail(h->runtime, play->menu.state.detail[0], play->menu.state.detail[1],
                                      (int16_t)play->menu.state.detail[2]);
    }
    /* sets the drawing flag. panels's scanner, gauges and clock
     * refresh all check it, and a cleared screen is the only thing that
     * drops it. The intro uses the same byte for its own overlay mode. */
    if (s->frontend == FE2_SESSION_PLAYING) s->drawing_flag = 0xff;
}
static void execute_frame(void *context, fe2_session *s)
{
    fe2_game_host *h = context;
    if (!h->in_frame) return;
    if (fe2_render_runtime_draw(h->runtime)) { fprintf(stderr, "Renderer draw failed\n"); s->error = 1; ++h->errors; }
    (void)s;
}
/* Assign publishes a slot once. A later upload to the same DAC index stays
 * until something writes the slot's colour again. */
static void restore_scene_palette(fe2_game_host *h)
{
    fe2_palette_cache *cache = fe2_render_runtime_palette(h->runtime);
    fe2_video *v = video(h);
    unsigned slot;
    if (!cache || !v) return;
    for (slot = 0; slot < FE2_PALETTE_SLOTS; ++slot) {
        const fe2_palette_slot *s = &cache->slots[slot];
        if (s->state < FE2_PALETTE_REPLACEABLE || s->dac < 0x80u) continue;
        fe2_video_palette(v, s->dac,
            (uint8_t)(((s->rgb12 >> 8) & 0xfu) * 4u + 3u),
            (uint8_t)(((s->rgb12 >> 4) & 0xfu) * 4u + 3u),
            (uint8_t)((s->rgb12 & 0xfu) * 4u + 3u));
    }
}
static void present_frame(void *context, fe2_session *s)
{
    fe2_game_host *h = context;
    int status;
    if (h->dac_clobbered && !h->palette_frame) {
        restore_scene_palette(h);
        h->dac_clobbered = 0;
    }
    h->palette_frame = 0;
    /* The session also presents outside a frame (the clear screens): the
     * page as it stands. */
    if (h->in_frame) status = fe2_render_runtime_present(h->runtime, h->present, h->present_context);
    else status = fe2_video_present(video(h), h->present, h->present_context);
    h->in_frame = 0;
    if (status) { fprintf(stderr, "Renderer present failed\n"); s->error = 1; ++h->errors; }
}
static void clear_screen(void *context)
{
    fe2_game_host *h = context;
    fe2_render_disable_drawing(&h->colour, &h->colour_services);
}
static void set_drawing_colour(void *context, uint16_t bx, uint8_t cl)
{
    fe2_game_host *h = context;
    fe2_render_rgb rgb;
    rgb.red = (uint8_t)bx; rgb.green = (uint8_t)(bx >> 8); rgb.blue = cl;
    fe2_render_save_and_set_color(&h->colour, &h->colour_services, rgb);
}
static void restore_drawing_colour(void *context)
{
    fe2_game_host_restore_color(context, 1);
}
void fe2_game_host_restore_color(fe2_game_host *h, int clear_view)
{
    fe2_render_restore_color(&h->colour, &h->colour_services, clear_view);
    if (h->session) h->session->render_mode = h->colour.restored;
}
static void clear_view(void *context)
{
    fe2_game_host *h = context;
    fe2_video_clear_view(video(h), 0);
}
static int render_record_bytes(fe2_game_host *h, const uint8_t *r, int alternate);

/* 0xf4 draws the exit scene's stack record from the auxiliary bank. Model 6
 * is the tombstone: its instances expand 0x9c00/0x9c01 (game over, obituary). */
static void render_record(void *context, fe2_session *s, fe2_object *record)
{
    fe2_game_host *h = context;
    uint8_t bytes[FE2_OBJECT_RECORD_BYTES];
    h->resources->text.current_day = s->pools.clock[1];
    fe2_object_write_record(record, bytes);
    (void)render_record_bytes(h, bytes, 1);
}

void fe2_game_host_session_services(fe2_game_host *h, fe2_session_services *v)
{
    v->context = h; v->begin_frame = begin_frame; v->execute_frame = execute_frame; v->present = present_frame;
    v->clear_screen = clear_screen; v->set_drawing_color = set_drawing_colour;
    v->restore_drawing_color = restore_drawing_colour; v->clear_view = clear_view; v->render_record = render_record;
}

/* ---- Rendering a record ----------------------------------------------------- */
static int render_record_bytes(fe2_game_host *h, const uint8_t *r, int alternate)
{
    fe2_render_model_transform t;
    fe2_render_program_result result;
    fe2_render_words variables, scales;
    uint16_t words[(RECORD_BYTES - RECORD_VARIABLES) / 2];
    fe2_render_bytes descriptor;
    uint16_t model = word_at(r + RECORD_MODEL);
    unsigned i;
    /* intro can present the clear page while restarting its timeline, then
     * submit the first scene again within the same session callback. */
    if (!h->in_frame && h->session) begin_frame(h, h->session);
    if (!h->in_frame) return -1;
    memset(&t, 0, sizeof(t));
    for (i = 0; i < 9; ++i) t.basis[i] = (int16_t)word_at(r + 2 * i);
    for (i = 0; i < 3; ++i) t.position[i] = (int32_t)dword_at(r + RECORD_OFFSET + 4 * i);
    if (h->session) {
        const fe2_resident *g = &h->session->resident;
        const uint32_t shades[4] = {g->classification_0, g->classification_1, g->classification_2, g->classification_3};
        for (i = 0; i < 3; ++i) t.direction[i] = (int16_t)h->session->light[i];
        for (i = 0; i < 8; ++i) t.shades[i] = (uint16_t)(shades[i / 2] >> (16 * (i % 2)));
    }
    if ((model & 1u) || model / 2 >= 240) return -1;
    descriptor = h->resources->models[alternate ? 1 : 0][model / 2];
    if (!descriptor.data || descriptor.count < DESCRIPTOR_EXPONENT + 2) return -1;
    t.exponent = word_at(descriptor.data + DESCRIPTOR_EXPONENT);
    t.material = MATERIAL;
    for (i = 0; i < (RECORD_BYTES - RECORD_VARIABLES) / 2; ++i) words[i] = word_at(r + RECORD_VARIABLES + 2 * i);
    variables.data = words; variables.count = (RECORD_BYTES - RECORD_VARIABLES) / 2;
    /* The scale operations read the resident sine table. */
    scales.data = (const uint16_t *)fe2_math_sine; scales.count = 2048;
    result = alternate ? fe2_render_runtime_submit_alternate(h->runtime, model, &t, variables, NULL, 0) :
        fe2_render_runtime_submit(h->runtime, model, &t, variables, scales);
    ++h->renders;
    if (result.status != FE2_RENDER_PROGRAM_COMPLETE) {
        fprintf(stderr, "Record model %04x failed: status=%u word=%zu bytes=%zu vertices=%u program=%u renders=%u\n",
            model, result.status, result.cursor, descriptor.count, word_at(descriptor.data + 4),
            word_at(descriptor.data), h->renders);
        ++h->errors; return -1;
    }
    return 0;
}
int fe2_game_host_render_record(fe2_game_host *h, const uint8_t *r)
{
    return render_record_bytes(h, r, 0);
}

/* ---- intro's providers ------------------------------------------------------ */
/* A bank: its size word, then the 240-slot directory of offsets from the
 * directory; each model runs to the bank's end. Texts install as module
 * strings for 0x4000..0x7fff (fe2-intro moduleStrings), preferring the
 * catalog already published at overlay[bank]. */
static void register_bank(void *opaque, const uint8_t *directory, const fe2_text_table *texts, uint16_t bank)
{
    fe2_game_host *h = opaque;
    size_t size = word_at(directory - 2), length = size >= 2 ? size - 2 : 0;
    unsigned i;
    /* A pending defer still holds the previous bank's module; keep saved_module. */
    if (h->module_pending_release) h->module_pending_release = 0;
    if (!h->bank_registered) {
        memcpy(h->saved_models, h->resources->models[0], sizeof(h->saved_models));
        if (!h->module_saved) {
            h->saved_module = h->resources->text.module;
            h->module_saved = 1;
        }
    }
    h->bank_registered = 1;
    for (i = 0; i < 240; ++i) {
        uint16_t at = word_at(directory + 2 * i);
        fe2_render_bytes b = {0, 0};
        if (at && at < length) { b.data = directory + at; b.count = length - at; }
        h->resources->models[0][i] = b;
    }
    /* Module strings for 0x4000: the runtime catalog already holds the bank's
     * text at overlay[bank] (native assets). texts is the same directory from
     * the overlay; prefer the catalog view for a known byte count. */
    (void)texts;
    if (bank < 64 && h->resources->text.overlay[bank].entries)
        h->resources->text.module = h->resources->text.overlay[bank];
}
static void release_bank(void *opaque)
{
    fe2_game_host *h = opaque;
    if (!h->bank_registered) return;
    memcpy(h->resources->models[0], h->saved_models, sizeof(h->saved_models));
    h->bank_registered = 0;
    /* Keep module for execute_frame label expand; begin_frame restores it. */
    h->module_pending_release = 1;
}
static void render_object(void *opaque, uint8_t *record, const fe2_call_args *args)
{
    (void)args;
    (void)fe2_game_host_render_record(opaque, record);
}
static void begin_overlay(void *opaque) { fe2_render_enable_drawing(&((fe2_game_host *)opaque)->colour); }
static void draw_sprite(void *opaque, uint16_t sprite, uint16_t x, uint16_t y)
{
    fe2_game_host *h = opaque;
    fe2_render_bytes record;
    if (fe2_graphics_cache_get(fe2_render_runtime_graphics(h->runtime), sprite, &record) ||
        fe2_video_blit(video(h), record, (int16_t)x, (int16_t)y, h->sprite_work, sizeof(h->sprite_work)))
        { fprintf(stderr, "Sprite %04x failed\n", sprite); ++h->errors; }
}
/* the expanded text, shadowed, at x, y in colour. */
static void draw_text(void *opaque, uint16_t text, uint8_t colour, uint16_t x, uint16_t y, const fe2_call_args *args)
{
    fe2_game_host *h = opaque;
    fe2_video *v = video(h);
    fe2_render_text_state state;
    uint8_t out[TEXT_BYTES];
    size_t length;
    (void)args;
    if (fe2_text_expand(&h->resources->text, text, out, sizeof(out) - 1, &length)) {
        fprintf(stderr, "Intro text %04x expansion failed\n", text); ++h->errors; return;
    }
    out[length] = 0;
    state.x = x; state.y = y; state.origin_x = x; state.color = colour;
    if (fe2_render_draw_text(v->pages[v->drawing], h->resources->font, h->resources->glyphs, h->colour_map,
                             out, length + 1, 1, &state)) ++h->errors;
}

void fe2_game_host_intro_services(fe2_game_host *h, fe2_intro_services *v)
{
    memset(v, 0, sizeof(*v));
    v->opaque = h; v->register_bank = register_bank; v->release_bank = release_bank;
    v->render_object = render_object; v->begin_overlay = begin_overlay; v->draw_sprite = draw_sprite;
    v->draw_text = draw_text;
}

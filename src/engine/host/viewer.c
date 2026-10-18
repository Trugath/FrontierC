/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "render/runtime.h"
#include "session/orbits.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "native_assets.h"
#ifndef FE2_NATIVE_SESSION
#define FE2_NATIVE_SESSION 0
#endif

typedef struct viewer {
    fe2_render_runtime *runtime;
    fe2_render_runtime_resources resources;
    fe2_render_model_transform transform;
    fe2_session session;
    fe2_session_orbits orbits;
    uint32_t pixels[64000];
    unsigned model, frames;
    int running, errors, session_mode;
    uint32_t time_step;
    HWND window;
} viewer;
static viewer app;
static void present(void *context, const uint8_t *pixels, const uint8_t *palette)
{
    viewer *v = context;
    unsigned i;
    for (i = 0; i < 64000; ++i) {
        const uint8_t *rgb = palette + pixels[i] * 3u;
        v->pixels[i] = (uint32_t)(rgb[0] * 255u / 63u) << 16 |
            (uint32_t)(rgb[1] * 255u / 63u) << 8 | rgb[2] * 255u / 63u;
    }
    ++v->frames;
    if (v->window) InvalidateRect(v->window, NULL, FALSE);
}
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_DESTROY) { app.running = 0; PostQuitMessage(0); return 0; }
    if (message == WM_KEYDOWN && w == VK_ESCAPE) { DestroyWindow(window); return 0; }
    if (message == WM_KEYDOWN && (w == VK_NEXT || w == VK_PRIOR)) {
        unsigned i;
        for (i = 0; i < 240; ++i) {
            app.model = (app.model + (w == VK_NEXT ? 1u : 239u)) % 240;
            if (app.resources.models[0][app.model].data) break;
        }
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        BITMAPINFO info;
        RECT rect;
        HDC dc = BeginPaint(window, &paint);
        memset(&info, 0, sizeof(info)); info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = 320; info.bmiHeader.biHeight = -200;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
        GetClientRect(window, &rect);
        StretchDIBits(dc, 0, 0, rect.right, rect.bottom, 0, 0, 320, 200, app.pixels, &info, DIB_RGB_COLORS, SRCCOPY);
        EndPaint(window, &paint); return 0;
    }
    return DefWindowProcA(window, message, w, l);
}
static int submit_scene(viewer *v)
{
    fe2_render_words values;
    uint16_t variables[128] = {0}, scales[16];
    fe2_render_program_result result;
    unsigned i;
    for (i = 0; i < 16; ++i) scales[i] = 256;
    values.data = variables; values.count = 128;
    if (v->session_mode) {
        variables[0] = v->session.records[2].clock;
        variables[1] = v->session.records[2].cargo_type;
    }
    result = fe2_render_runtime_submit(v->runtime, (uint16_t)(v->model * 2), &v->transform,
        values, (fe2_render_words){scales, 16});
    if (result.status != FE2_RENDER_PROGRAM_COMPLETE) {
        fprintf(stderr, "Model %u: status %u at word %zu\n", v->model, result.status, result.cursor);
        ++v->errors;
        return -1;
    }
    return 0;
}
static int frame(viewer *v)
{
    if (fe2_render_runtime_begin(v->runtime, 0)) return -1;
    (void)submit_scene(v);
    if (fe2_render_runtime_present(v->runtime, present, v)) { ++v->errors; return -1; }
    return 0;
}
static void session_begin(void *context, fe2_session *s)
{
    viewer *v = context;
    fe2_vertex_work *work = fe2_render_runtime_vertex_work(v->runtime);
    work->noise_a = s->noise[0]; work->noise_b = s->noise[1];
    if (fe2_render_runtime_begin(v->runtime, 0)) s->error = 1;
}
static void session_scene(void *context, fe2_session *s)
{
    viewer *v = context;
    if (submit_scene(v)) s->error = 1;
    s->noise[0] = fe2_render_runtime_vertex_work(v->runtime)->noise_a;
    s->noise[1] = fe2_render_runtime_vertex_work(v->runtime)->noise_b;
}
static void session_draw(void *context, fe2_session *s)
{
    viewer *v = context;
    if (fe2_render_runtime_draw(v->runtime)) s->error = 1;
}
static void session_present(void *context, fe2_session *s)
{
    viewer *v = context;
    if (fe2_render_runtime_present(v->runtime, present, v)) s->error = 1;
}
static void session_initialize(void *context, fe2_session *s, uint16_t selection)
{
    viewer *v = context;
    fe2_object *o = &s->records[2];
    if ((selection & 255u) != 2) return;
    o->role = 1; o->model = (uint16_t)(v->model * 2); o->flags = FE2_ORBIT_FIXED;
    o->as.body.docking_state = 1; /* Exercise orbits's frame-step scalar ramp. */
    s->objects.status[2] = FE2_SLOT_PERIODIC | FE2_SLOT_SECOND_UPDATE | FE2_SLOT_FRAME_RESET;
}
static int initialize_session(viewer *v)
{
    fe2_session_services services = {0};
    services.context = v; services.begin_frame = session_begin;
    services.execute_frame = session_draw; services.present = session_present;
    fe2_session_create(&v->session, &services);
    v->session.overlays[0].context = v;
    v->session.overlays[0].initialize = session_initialize;
    v->session.overlays[0].update = session_scene;
    v->session.overlays[0].metadata.update = 1;
    if (fe2_session_bind_orbits(&v->session, &v->orbits) || fe2_session_startup(&v->session) ||
        fe2_session_start(&v->session, 1)) return -1;
    v->session.render_mode = 128; v->session.time_step = v->time_step;
    return 0;
}
static int session_frame(viewer *v, uint32_t ticks)
{
    fe2_session_ticks(&v->session, ticks);
    if (fe2_session_step(&v->session) == FE2_SESSION_ERROR) {
        (void)fe2_render_runtime_abort(v->runtime); ++v->errors; return -1;
    }
    return 0;
}
int main(int argc, char **argv)
{
    unsigned i, headless = 0;
    memset(&app, 0, sizeof(app)); app.model = 2; app.running = 1;
    app.session_mode = FE2_NATIVE_SESSION; app.time_step = 994;
    for (i = 1; i < (unsigned)argc; ++i) {
        if (!strcmp(argv[i], "--frames") && i + 1 < (unsigned)argc) headless = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--model") && i + 1 < (unsigned)argc) app.model = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--textured")) app.resources.textured_surfaces = 1;
        else if (!strcmp(argv[i], "--session")) app.session_mode = 1;
        else if (!strcmp(argv[i], "--time-step") && i + 1 < (unsigned)argc) app.time_step = (uint32_t)strtoul(argv[++i], NULL, 10);
        else return 2;
    }
    native_resources(&app.resources, NULL);
    if (app.model >= 240 || !app.resources.models[0][app.model].data) return 2;
    app.runtime = fe2_render_runtime_create(&app.resources); if (!app.runtime) return 2;
    native_resources(&app.resources, fe2_render_runtime_graphics(app.runtime));
    for (i = 0; i < sizeof(native_palette) / 3; ++i) fe2_video_palette(fe2_render_runtime_video(app.runtime), (uint8_t)i,
        native_palette[i * 3], native_palette[i * 3 + 1], native_palette[i * 3 + 2]);
    app.transform.basis[0] = app.transform.basis[4] = app.transform.basis[8] = 32767;
    app.transform.position[2] = 4096; app.transform.exponent = 8;
    app.transform.direction[2] = -32768;
    for (i = 0; i < 8; ++i) app.transform.shades[i] = 17;
    if (app.session_mode && initialize_session(&app)) { fe2_render_runtime_destroy(app.runtime); return 2; }
    if (headless) {
        uint32_t hash = UINT32_C(2166136261);
        for (i = 0; i < headless; ++i) {
            fe2_math_rotate_pair(app.transform.basis, app.transform.basis + 2, 128);
            if (app.session_mode ? session_frame(&app, 4) : frame(&app)) break;
        }
        for (i = 0; i < 64000; ++i) hash = (hash ^ app.pixels[i]) * UINT32_C(16777619);
        printf("frames=%u errors=%d hash=%lu\n", app.frames, app.errors, (unsigned long)hash);
        if (app.session_mode) printf("time=%" PRIu64 " generation=%u scalar=%u\n",
            fe2_session_time(&app.session), app.session.generation, app.session.records[2].cargo_type);
    } else {
        WNDCLASSA wc;
        MSG message;
        ULONGLONG previous = GetTickCount64();
        memset(&wc, 0, sizeof(wc)); wc.lpfnWndProc = window_proc; wc.hInstance = GetModuleHandleA(NULL);
        wc.lpszClassName = "FE2NativeViewer"; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        if (!RegisterClassA(&wc)) { fe2_render_runtime_destroy(app.runtime); return 2; }
        app.window = CreateWindowA(wc.lpszClassName,
            app.session_mode ? "FE2 native session fixture | arrows rotate, W/S zoom, PgUp/PgDn model, Esc exit" :
                "FE2 native model viewer | arrows rotate, W/S zoom, PgUp/PgDn model, Esc exit",
            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 960, 640, NULL, NULL, wc.hInstance, NULL);
        if (!app.window) { fe2_render_runtime_destroy(app.runtime); return 2; }
        ShowWindow(app.window, SW_SHOW);
        while (app.running) {
            while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageA(&message); }
            if (!app.running) break;
            if (GetAsyncKeyState(VK_LEFT) & 0x8000) fe2_math_rotate_pair(app.transform.basis, app.transform.basis + 2, 256);
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) fe2_math_rotate_pair(app.transform.basis, app.transform.basis + 2, (uint16_t)-256);
            if (GetAsyncKeyState(VK_UP) & 0x8000) fe2_math_rotate_pair(app.transform.basis + 1, app.transform.basis + 2, 256);
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) fe2_math_rotate_pair(app.transform.basis + 1, app.transform.basis + 2, (uint16_t)-256);
            if ((GetAsyncKeyState('W') & 0x8000) && app.transform.position[2] > 128) app.transform.position[2] -= 64;
            if ((GetAsyncKeyState('S') & 0x8000) && app.transform.position[2] < 1048576) app.transform.position[2] += 64;
            if (app.session_mode) {
                ULONGLONG now = GetTickCount64(), elapsed = (now - previous) * 1000u;
                previous = now;
                while (elapsed > UINT32_MAX) { fe2_session_microseconds(&app.session, UINT32_MAX); elapsed -= UINT32_MAX; }
                fe2_session_microseconds(&app.session, (uint32_t)elapsed);
                if (fe2_session_step(&app.session) == FE2_SESSION_ERROR) {
                    (void)fe2_render_runtime_abort(app.runtime); ++app.errors;
                }
                Sleep(1);
            } else { (void)frame(&app); Sleep(16); }
        }
    }
    fe2_render_runtime_destroy(app.runtime); return app.errors ? 1 : 0;
}

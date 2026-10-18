/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Elliot Stirling
 * See LICENSE in the repository root.
 */

/* Native intro host: intro through the session until keys 1..5 start it.
 * --sound selects none, AdLib, Roland, or the PC speaker. */
#define _POSIX_C_SOURCE 200809L
#include "render/runtime.h"
#include "host/game_host.h"
#include "host/intro_audio.h"
#include "session/intro_overlay.h"
#include "host/gameplay.h"
#ifndef _WIN32
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <unistd.h>
#endif
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "asset_bind.h"
#ifndef FE2_NATIVE_GAME
#define FE2_NATIVE_GAME 0
#endif

enum { FRAME_W = 320, FRAME_H = 200, FRAME_PIXELS = 64000, SCALE = 3,
       VIEW_W = FRAME_W * SCALE, VIEW_H = FRAME_H * SCALE, STEP_US = 20000 };

typedef struct intro_app {
    fe2_game_host host;
    fe2_render_runtime *runtime;
    fe2_render_runtime_resources resources;
    fe2_session session;
    fe2_session_intro_overlay intro;
    fe2_intro_audio audio;
    fe2_gameplay game;
    uint32_t pixels[FRAME_PIXELS];
    uint32_t view[VIEW_W * VIEW_H];
    unsigned frames, ink;
    uint32_t intro_time;
    int errors, running, started, game_mode, no_autostart;
    uint16_t pending;
    int have_key;
    const char *dump_frame;
    int window_open, sound;
#ifdef _WIN32
    HWND window;
#else
    Display *display;
    Window window;
    XImage *image;
    GC gc;
#endif
} intro_app;

static intro_app app;
static fe2_asset_archive game_assets;

static int asset_path_beside_exe(char *out, size_t cap)
{
#ifdef _WIN32
    char module[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, module, MAX_PATH);
    char *slash, *other;
    size_t prefix;
    if (length == 0 || length >= MAX_PATH) return -1;
    slash = strrchr(module, '\\');
    other = strrchr(module, '/');
    if (other && (!slash || other > slash)) slash = other;
    if (!slash) return -1;
    prefix = (size_t)(slash - module) + 1;
    if (prefix + sizeof "frontier.dat" > cap) return -1;
    memcpy(out, module, prefix);
    memcpy(out + prefix, "frontier.dat", sizeof "frontier.dat");
    return 0;
#else
    char module[4096];
    ssize_t length = readlink("/proc/self/exe", module, sizeof module - 1);
    char *slash;
    size_t prefix;
    if (length <= 0 || (size_t)length >= sizeof module - 1) return -1;
    module[length] = 0;
    slash = strrchr(module, '/');
    if (!slash) return -1;
    prefix = (size_t)(slash - module) + 1;
    if (prefix + sizeof "frontier.dat" > cap) return -1;
    memcpy(out, module, prefix);
    memcpy(out + prefix, "frontier.dat", sizeof "frontier.dat");
    return 0;
#endif
}

static uint16_t poll_input(void *context)
{
    uint16_t code;
    (void)context;
    if (app.game.active) {
        int screenshot;
        return fe2_input_poll(&app.session.pointer, &screenshot);
    }
    if (!app.have_key) return 0;
    code = app.pending;
    app.have_key = 0;
    app.pending = 0;
    return code;
}

static void enqueue(uint16_t code)
{
    if (app.have_key) return;
    app.pending = code;
    app.have_key = 1;
}

/* Start-menu text 0x941a is drawn at (40, 20). Each CR drops 10 pixels, and
 * the five choices are separated by a blank line. */
static void intro_menu_click(intro_app *a, unsigned x, unsigned y)
{
    static const struct { unsigned top, bottom; uint16_t key; } row[] = {
        {32, 52, '1'}, {52, 72, '2'}, {72, 92, '3'}, {92, 112, '4'}, {112, 140, '5'}
    };
    unsigned i;
    if (a->game.active || a->session.pools.clock[0] < 0x9c2334u) return;
    if (x < 24 || x >= FRAME_W) return;
    for (i = 0; i < sizeof row / sizeof row[0]; ++i)
        if (y >= row[i].top && y < row[i].bottom) { enqueue(row[i].key); return; }
}
static void restore_input(void *context, uint16_t code)
{
    (void)context;
    if (app.game.active) fe2_input_restore(&app.session.pointer, (uint8_t)code);
    else enqueue(code);
}

static void paint(intro_app *a)
{
#ifdef _WIN32
    if (a->window) InvalidateRect(a->window, NULL, FALSE);
#else
    unsigned y, x;
    for (y = 0; y < VIEW_H; ++y) {
        unsigned sy = y / SCALE;
        for (x = 0; x < VIEW_W; ++x)
            a->view[y * VIEW_W + x] = a->pixels[sy * FRAME_W + x / SCALE];
    }
    if (!a->display) return;
    XPutImage(a->display, a->window, a->gc, a->image, 0, 0, 0, 0, VIEW_W, VIEW_H);
    XFlush(a->display);
#endif
}

static void present(void *context, const uint8_t *pixels, const uint8_t *palette)
{
    intro_app *a = context;
    unsigned i;
    for (i = 0; i < FRAME_PIXELS; ++i) {
        const uint8_t *rgb = palette + (size_t)pixels[i] * 3u;
        a->pixels[i] = (uint32_t)(rgb[0] * 255u / 63u) << 16 |
            (uint32_t)(rgb[1] * 255u / 63u) << 8 | (uint32_t)(rgb[2] * 255u / 63u);
    }
    ++a->frames;
    for (i = 0; i < FRAME_PIXELS; ++i) if (pixels[i] && pixels[i] != 0x7f) ++a->ink;
    paint(a);
}

#ifdef _WIN32
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_DESTROY) {
        app.window = NULL;
        app.window_open = 0;
        app.running = 0;
        PostQuitMessage(0);
        return 0;
    }
    if ((message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) && app.game.active) {
        if (message == WM_SYSKEYDOWN && w == VK_F4) return DefWindowProcA(window, message, w, l);
        fe2_gameplay_key(&app.game, (uint8_t)((l >> 16) & 0x7f), (l & (1L << 24)) != 0,
            message == WM_KEYUP || message == WM_SYSKEYUP);
        return 0;
    }
    if (message == WM_MOUSEMOVE || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
        message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) {
        RECT client;
        int px = (int16_t)(l & 0xffff), py = (int16_t)((l >> 16) & 0xffff);
        unsigned x = 0, y = 0;
        GetClientRect(window, &client);
        if (client.right > 0 && client.bottom > 0) {
            x = (unsigned)(px < 0 ? 0 : px * FRAME_W / client.right);
            y = (unsigned)(py < 0 ? 0 : py * FRAME_H / client.bottom);
        }
        if (app.game.active) fe2_gameplay_mouse(&app.game, x, y,
            (w & MK_LBUTTON ? 1u : 0u) | (w & MK_RBUTTON ? 2u : 0u));
        else if (message == WM_LBUTTONDOWN) intro_menu_click(&app, x, y);
        if (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN) SetCapture(window);
        if (!(w & (MK_LBUTTON | MK_RBUTTON))) ReleaseCapture();
        return 0;
    }
    if (message == WM_KILLFOCUS && app.game.active) {
        app.game.mouse_buttons = 0;
        fe2_input_init(&app.session.input);
        return 0;
    }
    /* 1..5 choose a start. Any other key jumps the intro clock to the
     * start menu. Repeats stay queued once so a held key does not also
     * confirm the first start. */
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !app.game.active) {
        if (message == WM_SYSKEYDOWN && w == VK_F4) return DefWindowProcA(window, message, w, l);
        if (!(l & (1L << 30))) enqueue(w >= '1' && w <= '5' ? (uint16_t)w : (uint16_t)' ');
        return 0;
    }
    if (message == WM_CHAR && !app.game.active && w >= '1' && w <= '5') {
        enqueue((uint16_t)w);
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint_info;
        BITMAPINFO info;
        RECT rect;
        HDC dc = BeginPaint(window, &paint_info);
        memset(&info, 0, sizeof(info));
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = FRAME_W;
        info.bmiHeader.biHeight = -FRAME_H;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        GetClientRect(window, &rect);
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, 0, 0, rect.right, rect.bottom, 0, 0, FRAME_W, FRAME_H,
            app.pixels, &info, DIB_RGB_COLORS, SRCCOPY);
        EndPaint(window, &paint_info);
        return 0;
    }
    return DefWindowProcA(window, message, w, l);
}

static void pump_keys(intro_app *a)
{
    MSG message;
    while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) { a->running = 0; break; }
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

static int open_window(intro_app *a)
{
    WNDCLASSA wc;
    RECT rect = {0, 0, VIEW_W, VIEW_H};
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = a->game_mode ? "FE2NativeGame" : "FE2NativeIntro";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassA(&wc)) return -1;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    a->window = CreateWindowA(wc.lpszClassName,
        a->game_mode ? "FE2 native game | any key skips to the start menu, 1-5 start, F7 take off, Enter/Shift speed, A/Z ,/. turn, F1 view, Esc menu, Alt+F4 quit" :
            "FE2 intro | any key skips to the start menu, 1-5 start, Alt+F4 quit",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, wc.hInstance, NULL);
    if (!a->window) return -1;
    a->window_open = 1;
    ShowWindow(a->window, SW_SHOW);
    return 0;
}

static void close_window(intro_app *a)
{
    if (a->window) DestroyWindow(a->window);
}

static uint64_t monotonic_us(void)
{
    LARGE_INTEGER counter, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart / frequency.QuadPart) * UINT64_C(1000000) +
        (uint64_t)(counter.QuadPart % frequency.QuadPart) * UINT64_C(1000000) /
        (uint64_t)frequency.QuadPart;
}
#else
static uint8_t x_scan(KeySym symbol, int *extended)
{
    static const uint8_t letters[26] = {0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,
        0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
    *extended = 0;
    if (symbol >= XK_A && symbol <= XK_Z) symbol += XK_a - XK_A;
    if (symbol >= XK_a && symbol <= XK_z) return letters[symbol - XK_a];
    if (symbol >= XK_F1 && symbol <= XK_F10) return (uint8_t)(0x3b + symbol - XK_F1);
    if (symbol >= XK_1 && symbol <= XK_9) return (uint8_t)(2 + symbol - XK_1);
    switch (symbol) {
    case XK_0: return 0x0b; case XK_Return: return 0x1c; case XK_space: return 0x39;
    case XK_Shift_L: return 0x2a; case XK_Shift_R: return 0x36;
    case XK_Control_L: return 0x1d; case XK_Alt_L: return 0x38;
    case XK_comma: return 0x33; case XK_period: return 0x34;
    case XK_slash: return 0x35; case XK_Tab: return 0x0f; case XK_BackSpace: return 0x0e;
    case XK_Escape: return 0x01;
    case XK_Left: *extended = 1; return 0x4b;
    case XK_Right: *extended = 1; return 0x4d;
    case XK_Up: *extended = 1; return 0x48;
    case XK_Down: *extended = 1; return 0x50;
    default: return 0;
    }
}
static void pump_keys(intro_app *a)
{
    while (a->display && XPending(a->display)) {
        XEvent event;
        char text[8];
        KeySym sym;
        int length;
        XNextEvent(a->display, &event);
        if (event.type == Expose) paint(a);
        if (event.type == ButtonPress && event.xbutton.button == 1 && !a->game.active)
            intro_menu_click(a, event.xbutton.x / SCALE, event.xbutton.y / SCALE);
        if (event.type == FocusOut && a->game.active) { fe2_input_init(&a->session.input); continue; }
        if (event.type != KeyPress && event.type != KeyRelease) continue;
        length = XLookupString(&event.xkey, text, (int)sizeof(text), &sym, NULL);
        if (a->game.active) {
            int extended; uint8_t scan = x_scan(XLookupKeysym(&event.xkey, 0), &extended);
            if (scan) fe2_gameplay_key(&a->game, scan, extended, event.type == KeyRelease);
        } else if (event.type == KeyPress) {
            uint16_t code = length > 0 && text[0] >= '1' && text[0] <= '5' ? (uint8_t)text[0] : (uint16_t)' ';
            enqueue(code);
        }
    }
}

static int open_window(intro_app *a)
{
    XSetWindowAttributes attr;
    int screen;
    a->display = XOpenDisplay(NULL);
    if (!a->display) { fprintf(stderr, "No X11 display\n"); return -1; }
    screen = DefaultScreen(a->display);
    memset(&attr, 0, sizeof(attr));
    attr.background_pixel = BlackPixel(a->display, screen);
    attr.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | FocusChangeMask | ExposureMask;
    a->window = XCreateWindow(a->display, RootWindow(a->display, screen), 0, 0, VIEW_W, VIEW_H, 0,
        CopyFromParent, InputOutput, CopyFromParent, CWBackPixel | CWEventMask, &attr);
    XStoreName(a->display, a->window, "FE2 intro | 1-5 start, Esc menu, Alt+F4 quit");
    a->gc = XCreateGC(a->display, a->window, 0, NULL);
    a->image = XCreateImage(a->display, DefaultVisual(a->display, screen), 24, ZPixmap, 0,
        (char *)a->view, VIEW_W, VIEW_H, 32, 0);
    if (!a->image) { fprintf(stderr, "XCreateImage failed\n"); return -1; }
    XMapWindow(a->display, a->window);
    XFlush(a->display);
    a->window_open = 1;
    return 0;
}

static void close_window(intro_app *a)
{
    if (!a->display) return;
    if (a->image) { a->image->data = NULL; XDestroyImage(a->image); }
    if (a->gc) XFreeGC(a->display, a->gc);
    XCloseDisplay(a->display);
    a->display = NULL;
    a->window_open = 0;
}

static uint64_t monotonic_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000000u + (uint64_t)now.tv_nsec / 1000u;
}
#endif

static void select_music(void *context, int16_t index)
{
    (void)context;
    fe2_intro_audio_select_music(&app.audio, index);
}
static void check_music(void *context)
{
    (void)context;
    fe2_intro_audio_check_music(&app.audio);
}
static void reset_music(void *context)
{
    (void)context;
    fe2_intro_audio_reset_music(&app.audio);
}
static void stop_effects(void *context)
{
    (void)context;
    fe2_intro_audio_stop_effects(&app.audio);
}
static void play_level(void *context, uint16_t effect, uint8_t level)
{
    (void)context;
    fe2_intro_audio_play_level(&app.audio, effect, level);
}

static int pump_input(void *context)
{
    (void)context;
    pump_keys(&app);
#ifdef _WIN32
    Sleep(1);
#else
    struct timespec pause = {0, 1000000};
    nanosleep(&pause, NULL);
#endif
    return app.running ? 0 : 1;
}
static int run(intro_app *a, unsigned limit)
{
    fe2_session_services services;
    uint64_t previous;
    fe2_intro_audio_init(&a->audio, &a->session, a->window_open, a->sound);
    fe2_intro_audio_bind_scores(&a->audio, &game_assets);
    fe2_intro_audio_bind_sound(&a->audio, &a->resources);
    memset(&services, 0, sizeof(services));
    fe2_game_host_session_services(&a->host, &services);
    services.poll_input = poll_input;
    a->host.pump = pump_input;
    services.restore_input = restore_input;
    services.select_music = select_music;
    services.check_music = check_music;
    services.reset_music = reset_music;
    services.stop_effects = stop_effects;
    services.play_level = play_level;
    fe2_session_create(&a->session, &services);
    fe2_game_host_attach(&a->host, &a->session);
    fe2_game_host_intro_services(&a->host, &a->intro.host);
    if (fe2_session_bind_intro_overlay(&a->session, &a->intro) ||
        fe2_session_startup(&a->session) || fe2_session_intro(&a->session)) return -1;
    /* stays clear until preferences enable keys. This host is the test window. */
    a->session.input_mask = 0xff;
    if (a->intro_time) a->session.pools.clock[0] = a->intro_time;
    previous = monotonic_us();
    while (a->running) {
        fe2_session_result result;
        if (a->window_open) pump_keys(a);
        if (!a->running) break;
        /* The opening frames are black. Inject once a scene has drawn. */
        if (limit && !a->no_autostart && a->ink && !a->have_key && !a->started) enqueue('1');
        if (a->window_open) {
            uint64_t now = monotonic_us(), elapsed = now - previous;
            previous = now;
            while (elapsed > UINT32_MAX) {
                fe2_session_microseconds(&a->session, UINT32_MAX);
                fe2_intro_audio_advance(&a->audio, UINT32_MAX);
                elapsed -= UINT32_MAX;
            }
            fe2_session_microseconds(&a->session, (uint32_t)elapsed);
            fe2_intro_audio_advance(&a->audio, (uint32_t)elapsed);
        } else {
            fe2_session_microseconds(&a->session, STEP_US);
            fe2_intro_audio_advance(&a->audio, STEP_US);
        }
        result = fe2_session_step(&a->session);
        if (result == FE2_SESSION_ERROR || a->session.error) {
            fprintf(stderr, "Session stopped: frontend=%u view=%u mode=%02x scene=%u host_errors=%u "
                "overlay=%u current=%u map=%d mode %d star=%s jump=%u regions=%u\n",
                a->session.frontend, a->game.flight.state.view_index, a->session.resident.flight_mode,
                a->game.scenes, a->host.errors, a->session.selected_overlay, a->session.current_overlay,
                a->game.map.status, a->game.map.state.mode,
                a->game.map.state.selected_name[0] ? a->game.map.state.selected_name : "-",
                a->game.flight.state.jump_enabled,
                (unsigned)fe2_pointer_regions_count(&a->session.regions));
            ++a->errors; break;
        }
        if (result == FE2_SESSION_STARTED) {
            a->started = 1;
            printf("started selection=%u\n", a->session.selection);
            fflush(stdout);
            if (a->game_mode) {
                if (fe2_gameplay_start(&a->game, &a->host, &a->audio, a->session.selection)) { ++a->errors; break; }
            } else if (limit) break;
        }
        if (result == FE2_SESSION_EXIT) break;
        if (limit && a->frames >= limit && a->started) break;
        if (limit && a->frames >= limit) break;
        if (a->window_open) {
#ifdef _WIN32
            Sleep(1);
#else
            struct timespec pause = {0, 1000000};
            nanosleep(&pause, NULL);
#endif
        }
    }
    return a->errors || a->host.errors ? -1 : 0;
}

static int parse_sound(const char *text, int *device)
{
    if (!strcmp(text, "none")) { *device = FE2_HOST_SOUND_NONE; return 0; }
    if (!strcmp(text, "adlib")) { *device = FE2_HOST_SOUND_ADLIB; return 0; }
    if (!strcmp(text, "roland")) { *device = FE2_HOST_SOUND_ROLAND; return 0; }
    if (!strcmp(text, "speaker")) { *device = FE2_HOST_SOUND_SPEAKER; return 0; }
    return -1;
}

int main(int argc, char **argv)
{
    unsigned i, limit = 0;
    uint32_t hash = UINT32_C(2166136261);
    int status;
    char asset_file[4096];
    const char *asset_path = NULL;
    const fe2_asset_entry *palette;
    memset(&app, 0, sizeof(app));
    app.running = 1;
    app.game_mode = FE2_NATIVE_GAME;
    app.sound = FE2_HOST_SOUND_ADLIB;
    for (i = 1; i < (unsigned)argc; ++i) {
        if (!strcmp(argv[i], "--frames") && i + 1 < (unsigned)argc) limit = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--intro-time") && i + 1 < (unsigned)argc) app.intro_time = (uint32_t)strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--no-autostart")) app.no_autostart = 1;
        else if (!strcmp(argv[i], "--game")) app.game_mode = 1;
        else if (!strcmp(argv[i], "--dump-frame") && i + 1 < (unsigned)argc) app.dump_frame = argv[++i];
        else if (!strcmp(argv[i], "--assets") && i + 1 < (unsigned)argc) asset_path = argv[++i];
        else if (!strcmp(argv[i], "--sound")) {
            if (i + 1 >= (unsigned)argc || parse_sound(argv[++i], &app.sound)) {
                fprintf(stderr, "Sound choices: none, adlib, roland, speaker\n");
                return 2;
            }
        }
        else return 2;
    }
    if (!asset_path) {
        if (asset_path_beside_exe(asset_file, sizeof asset_file)) {
            fprintf(stderr, "Could not find frontier.dat beside this program.\n");
            return 2;
        }
        asset_path = asset_file;
    }
    if (fe2_asset_archive_load(&game_assets, asset_path) ||
        fe2_asset_archive_apply(&game_assets, &app.resources, NULL) ||
        fe2_asset_bind_install(&game_assets)) {
        fprintf(stderr, "Could not read %s. Unpack your Frontier install and pack frontier.dat with frontier-data.\n", asset_path);
        fe2_asset_archive_unload(&game_assets);
        return 2;
    }
    app.runtime = fe2_render_runtime_create(&app.resources);
    if (!app.runtime) { fe2_asset_archive_unload(&game_assets); return 2; }
    if (fe2_asset_archive_apply(&game_assets, &app.resources, fe2_render_runtime_graphics(app.runtime))) {
        fprintf(stderr, "Could not read %s. Unpack your Frontier install and pack frontier.dat with frontier-data.\n", asset_path);
        fe2_render_runtime_destroy(app.runtime);
        fe2_asset_archive_unload(&game_assets);
        return 2;
    }
    palette = fe2_asset_archive_find(&game_assets, "palette");
    if (!palette || palette->count == 0 || palette->count % 3 || palette->count / 3 > 256) {
        fprintf(stderr, "Could not read %s. Unpack your Frontier install and pack frontier.dat with frontier-data.\n", asset_path);
        fe2_render_runtime_destroy(app.runtime);
        fe2_asset_archive_unload(&game_assets);
        return 2;
    }
    for (i = 0; i < palette->count / 3; ++i)
        fe2_video_palette(fe2_render_runtime_video(app.runtime), (uint8_t)i,
            palette->data[i * 3], palette->data[i * 3 + 1], palette->data[i * 3 + 2]);
    fe2_game_host_init(&app.host, app.runtime, &app.resources, present, &app);
    if (!limit && open_window(&app)) {
        fe2_render_runtime_destroy(app.runtime);
        fe2_asset_archive_unload(&game_assets);
        return 2;
    }
    status = run(&app, limit);
    for (i = 0; i < FRAME_PIXELS; ++i) hash = (hash ^ app.pixels[i]) * UINT32_C(16777619);
    printf("frames=%u errors=%d hash=%lu selection=%u\n", app.frames,
        app.errors + app.host.errors, (unsigned long)hash, app.session.selection);
    if (app.game_mode) printf("game=%d bodies=%u scenes=%u submitted=%u\n", app.game.active,
        app.game.bodies, app.game.scenes, app.game.submitted);
    if (app.game.active) printf("flight mode=%02x throttle=%ld\n", app.session.resident.flight_mode,
        (long)(int32_t)app.session.resident.throttle);
    if (app.dump_frame) {
        FILE *file = fopen(app.dump_frame, "wb");
        if (!file) status = -1;
        else {
            fprintf(file, "P6\n320 200\n255\n");
            for (i = 0; i < FRAME_PIXELS; ++i) {
                uint8_t rgb[3] = {(uint8_t)(app.pixels[i] >> 16), (uint8_t)(app.pixels[i] >> 8), (uint8_t)app.pixels[i]};
                if (fwrite(rgb, 1, 3, file) != 3) { status = -1; break; }
            }
            if (fclose(file)) status = -1;
        }
    }
    close_window(&app);
    fe2_intro_audio_close(&app.audio);
    fe2_render_runtime_destroy(app.runtime);
    fe2_asset_archive_unload(&game_assets);
    return status ? 1 : 0;
}

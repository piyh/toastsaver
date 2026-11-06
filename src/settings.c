/* The settings page, reached from the gear in the picker's corner. It is a table of rows
   (SaverOption, drawn by the page in options.c) in groups: when it starts, taking turns,
   what's on screen and this computer, then a test run and a reset to the
   defaults. Only settings for the whole screensaver live here: a saver's own settings
   (declared in its own table) are on its own page, opened from its tile in the picker.
   Every change is saved at once; timing changes restart the idle watcher when the page
   closes so they apply without logging out. The rows that need the idle watcher are
   greyed out where it isn't installed (the Mac preview), instead of doing nothing.
   The minutes rows step through a list with - and +, keep going past its end, and take
   any number typed in after tapping the value. This file also has the look the pages share. */
#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const int IDLE_OPTS[] = { 1, 2, 3, 5, 10, 15, 20, 30, 45, 60 };
static const int OFF_OPTS[] = { 0, 5, 10, 15, 20, 30, 45, 60, 90, 120 };
static const int CYCLE_OPTS[] = { 0, 2, 5, 10, 15, 20, 30, 45, 60, 120 };
#define NOPTS 10
#define PAST_END 30   /* past the end of a list, - and + move this many minutes */

static int watcher;         /* is the idle watcher installed? (not on the Mac preview) */
static int timingChanged;   /* restart the watcher when the page closes */

/* ---------- when rows can be changed ---------- */

static int live_watcher(void) { return watcher; }
static int live_timing(void) { return watcher && g_config.enabled; }
static int live_enabled(void) { return g_config.enabled; }
static int live_clock24(void) { return g_config.enabled && g_config.clock; }

/* ---------- what a change sets off ---------- */

static void timing(const SaverOption *o, int old, int dir) {
    (void)o; (void)old; (void)dir;
    timingChanged = 1;
}

/* How "screen off" steps: through OFF_OPTS, then on in PAST_END steps. */
static const SaverOption OFF_STEPS = { .label = "", .hi = CONFIG_MAX_MINUTES, .step = PAST_END,
                                       .stops = OFF_OPTS, .nstops = NOPTS };

/* Keep "screen off" later than "screensaver after" (or never): on to a later time when it isn't,
   except that stepping "screen off" itself down onto a too-early time means NEVER. */
static void keep_order(const SaverOption *o, int old, int dir) {
    int *off = &g_config.screenOffMinutes, idle = g_config.idleMinutes;
    timingChanged = 1;
    if (*off == 0 || *off > idle) return;
    if (o->value == off && dir < 0) { *off = 0; return; }
    int v = *off;
    while (v != 0 && v <= idle) {
        int next = option_step(&OFF_STEPS, v, 1);
        if (next == v) { *off = o->value == off ? old : 0; return; }
        v = next;
    }
    *off = v;
}

/* ---------- the rows ---------- */

static const SaverOption ROWS[] = {
    { .label = "SCREENSAVER", .group = "WHEN IT STARTS", .value = &g_config.enabled, .def = 1, .hi = 1,
      .flags = OPT_TOGGLE, .live = live_watcher, .changed = timing },
    { .label = "START AFTER", .value = &g_config.idleMinutes, .def = 5, .lo = 1, .hi = CONFIG_MAX_MINUTES,
      .step = PAST_END, .stops = IDLE_OPTS, .nstops = NOPTS, .unit = "MIN", .live = live_timing, .changed = keep_order },
    { .label = "SCREEN OFF AFTER", .value = &g_config.screenOffMinutes, .def = 30, .hi = CONFIG_MAX_MINUTES,
      .step = PAST_END, .stops = OFF_OPTS, .nstops = NOPTS, .unit = "MIN", .zero = "NEVER", .live = live_timing,
      .changed = keep_order },
    { .label = "TAKE TURNS EVERY", .group = "WHEN SEVERAL HAVE A HEART", .value = &g_config.cycleMinutes, .def = 10,
      .hi = CONFIG_MAX_MINUTES, .step = PAST_END, .stops = CYCLE_OPTS, .nstops = NOPTS, .unit = "MIN", .zero = "NEVER",
      .live = live_enabled },
    { .label = "SHOW CLOCK", .group = "ON SCREEN", .value = &g_config.clock, .hi = 1, .flags = OPT_TOGGLE,
      .live = live_enabled },
    { .label = "CLOCK STYLE", .value = &g_config.clock24, .hi = 1, .flags = OPT_TOGGLE, .off = "12 HOUR", .on = "24 HOUR",
      .live = live_clock24 },
    { .label = "SAVE POWER", .group = "THIS COMPUTER", .value = &g_config.fps, .def = 30, .hi = 1000, .flags = OPT_TOGGLE,
      .onValue = 30, .on = "30 FPS" },
    { 0 },
};

/* RESET: everything on this page back to how a new install starts (the saver choice and name
   stay, and so do the savers' own settings, which have a RESET of their own). */
static void reset_defaults(void) {
    const Config *d = &CONFIG_DEFAULTS;
    g_config.enabled = d->enabled;
    g_config.idleMinutes = d->idleMinutes;
    g_config.screenOffMinutes = d->screenOffMinutes;
    g_config.cycleMinutes = d->cycleMinutes;
    g_config.clock = d->clock;
    g_config.clock24 = d->clock24;
    g_config.fps = d->fps;
    timingChanged = 1;
}

static const char *note(void) {
    return watcher ? NULL : "STARTING BY ITSELF WHEN IDLE WORKS ON LINUX ONLY";
}

static const PageSpec SPEC = {
    .title = "SETTINGS", .rows = ROWS, .reset = reset_defaults,
    .resetNote = "TAP SURE? TO PUT EVERY SETTING HERE BACK", .note = note,
};

const PageSpec *settings_spec(void) {
    watcher = access(TOASTSAVER_BINDIR "/toastsaver-idle", X_OK) == 0;
    return &SPEC;
}

int run_settings(SDL_Window *win, SDL_Renderer *r) {
    timingChanged = 0;
    int quit = run_page(win, r, settings_spec());
    /* New timings take effect now rather than at next login. */
    if (timingChanged && watcher)
        if (system(TOASTSAVER_BINDIR "/toastsaver-idle --restart >/dev/null 2>&1") != 0)
            fprintf(stderr, "toastsaver: could not restart the idle watcher\n");
    return quit;
}

/* ---------- the look the pages share ---------- */

void ui_text_at(SDL_Renderer *r, const char *t, int x, int cy, int sc, SDL_Color c) {
    draw_text_shadow(r, t, x, cy - FONT_H * sc / 2, sc, c);
}

void ui_round_button(SDL_Renderer *r, SDL_Rect b, int plus, int enabled) {
    float cx = b.x + b.w / 2.0f, cy = b.y + b.h / 2.0f, rad = b.w / 2.0f;
    fill_circle(r, cx, cy, rad, enabled ? hexcol(0xFFFFFF) : hexcol(0x5A6B7A));
    fill_circle(r, cx, cy, rad * 0.86f, enabled ? hexcol(0x1E88E5) : hexcol(0x3A4A58));
    SDL_Color w = enabled ? hexcol(0xFFFFFF) : hexcol(0x8A9AAA);
    thick_line(r, cx - rad * 0.45f, cy, cx + rad * 0.45f, cy, rad * 0.22f, w);
    if (plus) thick_line(r, cx, cy - rad * 0.45f, cx, cy + rad * 0.45f, rad * 0.22f, w);
}

void ui_toggle(SDL_Renderer *r, SDL_Rect t, int on, int enabled) {
    SDL_Color track = !enabled ? hexcol(0x3A4A58) : on ? hexcol(0x43A047) : hexcol(0x6B7280);
    fill_rrect(r, t, t.h / 2, track);
    float rad = t.h * 0.42f, cx = on ? t.x + t.w - t.h / 2.0f : t.x + t.h / 2.0f;
    fill_circle(r, cx, t.y + t.h / 2.0f, rad, enabled ? hexcol(0xFFFFFF) : hexcol(0x8A9AAA));
}

void ui_button(SDL_Renderer *r, SDL_Rect b, const char *label, uint32_t col, uint32_t dark) {
    fill_rrect(r, (SDL_Rect){ b.x, b.y + b.h / 12, b.w, b.h }, b.h / 3, hexcol(dark));
    fill_rrect(r, b, b.h / 3, hexcol(col));
    int sc = (int)(b.h * 0.42f / FONT_H);
    while (sc > 1 && text_width(label, sc) > b.w * 0.85f) sc--;
    ui_text_at(r, label, b.x + (b.w - text_width(label, sc)) / 2, b.y + b.h / 2, sc, hexcol(0xFFFFFF));
}

int ui_hit(SDL_Rect rc, int x, int y) { return x >= rc.x && y >= rc.y && x < rc.x + rc.w && y < rc.y + rc.h; }

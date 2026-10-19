/* The settings page, reached from the picker's gear (held down, so kids don't wander in)
   or with toastsaver-settings: on/off, timings, clock, power saving and a test run.
   Every change is saved at once; timing changes restart the idle watcher when the page
   closes so they apply without logging out. The rows that need the idle watcher are
   greyed out where it isn't installed (the Mac preview), instead of doing nothing. */
#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ROWS 6
static const int IDLE_OPTS[] = { 1, 2, 3, 5, 10, 15, 20, 30, 45, 60 };
static const int OFF_OPTS[] = { 0, 5, 10, 15, 20, 30, 45, 60, 90, 120 };
#define NOPTS 10

typedef struct {
    SDL_Renderer *r;
    int w, h, quit, timingChanged, hover, watcher;
    float t, rowH;
    SDL_Rect row[ROWS], minus[ROWS], plus[ROWS], toggle[ROWS], test, done;
} Settings;

enum { R_ENABLED, R_IDLE, R_OFF, R_CLOCK, R_CLOCK24, R_POWER };   /* row order */

static void layout(Settings *s) {
    float top = s->h * 0.15f, bottom = s->h * 0.81f;
    s->rowH = (bottom - top) / ROWS;
    int pad = (int)(s->rowH * 0.1f), btn = (int)(s->rowH * 0.62f);
    for (int i = 0; i < ROWS; i++) {
        SDL_Rect rw = { s->w / 20, (int)(top + i * s->rowH) + pad / 2, s->w - s->w / 10, (int)s->rowH - pad };
        s->row[i] = rw;
        int cy = rw.y + (rw.h - btn) / 2, right = rw.x + rw.w - pad * 2;
        s->plus[i] = (SDL_Rect){ right - btn, cy, btn, btn };
        s->minus[i] = (SDL_Rect){ (int)(s->w * 0.56f), cy, btn, btn };
        s->toggle[i] = (SDL_Rect){ (int)(s->w * 0.56f), cy, btn * 2, btn };
    }
    int bh = (int)(s->h * 0.11f), bw = s->w / 4, by = (int)(s->h * 0.84f);
    s->test = (SDL_Rect){ s->w / 2 - bw - s->w / 40, by, bw, bh };
    s->done = (SDL_Rect){ s->w / 2 + s->w / 40, by, bw, bh };
}

void *settings_create(SDL_Renderer *r, int w, int h) {
    Settings *s = calloc(1, sizeof *s);
    s->r = r;
    s->w = w;
    s->h = h;
    s->hover = -1;
    s->watcher = access(TOASTSAVER_BINDIR "/toastsaver-idle", X_OK) == 0;
    layout(s);
    return s;
}

void settings_destroy(void *p) { free(p); }
void settings_step(void *p, float dt) { ((Settings *)p)->t += dt; }

static int index_of(const int *opts, int v) {
    for (int i = 0; i < NOPTS; i++) if (opts[i] >= v) return i;
    return NOPTS - 1;
}

/* Keep "screen off" later than "screensaver after" (or never). */
static void fix_order(int idleChanged) {
    int off = g_config.screenOffMinutes, idle = g_config.idleMinutes;
    if (off == 0 || off > idle) return;
    if (idleChanged) {
        int i = index_of(OFF_OPTS, idle + 1);
        g_config.screenOffMinutes = OFF_OPTS[i] > idle ? OFF_OPTS[i] : 0;
    } else {
        g_config.screenOffMinutes = 0;
    }
}

static void stepper(int row, int dir) {
    if (row == R_IDLE) {
        int i = index_of(IDLE_OPTS, g_config.idleMinutes) + dir;
        if (i < 0 || i >= NOPTS) return;
        g_config.idleMinutes = IDLE_OPTS[i];
        fix_order(1);
    } else {
        int i = index_of(OFF_OPTS, g_config.screenOffMinutes) + dir;
        while (i > 0 && i < NOPTS && OFF_OPTS[i] <= g_config.idleMinutes) i += dir;  /* skip too-early values */
        if (i < 0 || i >= NOPTS) return;
        g_config.screenOffMinutes = OFF_OPTS[i];
    }
}

static void text_at(SDL_Renderer *r, const char *t, int x, int cy, int sc, SDL_Color c) {
    draw_text_shadow(r, t, x, cy - FONT_H * sc / 2, sc, c);
}

static void round_button(SDL_Renderer *r, SDL_Rect b, int plus, int enabled) {
    float cx = b.x + b.w / 2.0f, cy = b.y + b.h / 2.0f, rad = b.w / 2.0f;
    fill_circle(r, cx, cy, rad, enabled ? hexcol(0xFFFFFF) : hexcol(0x5A6B7A));
    fill_circle(r, cx, cy, rad * 0.86f, enabled ? hexcol(0x1E88E5) : hexcol(0x3A4A58));
    SDL_Color w = enabled ? hexcol(0xFFFFFF) : hexcol(0x8A9AAA);
    thick_line(r, cx - rad * 0.45f, cy, cx + rad * 0.45f, cy, rad * 0.22f, w);
    if (plus) thick_line(r, cx, cy - rad * 0.45f, cx, cy + rad * 0.45f, rad * 0.22f, w);
}

static void toggle_switch(SDL_Renderer *r, SDL_Rect t, int on, int enabled, float tt) {
    (void)tt;
    SDL_Color track = !enabled ? hexcol(0x3A4A58) : on ? hexcol(0x43A047) : hexcol(0x6B7280);
    fill_rrect(r, t, t.h / 2, track);
    float rad = t.h * 0.42f, cx = on ? t.x + t.w - t.h / 2.0f : t.x + t.h / 2.0f;
    fill_circle(r, cx, t.y + t.h / 2.0f, rad, enabled ? hexcol(0xFFFFFF) : hexcol(0x8A9AAA));
}

static void button(SDL_Renderer *r, SDL_Rect b, const char *label, uint32_t col, uint32_t dark) {
    fill_rrect(r, (SDL_Rect){ b.x, b.y + b.h / 12, b.w, b.h }, b.h / 3, hexcol(dark));
    fill_rrect(r, b, b.h / 3, hexcol(col));
    int sc = (int)(b.h * 0.42f / FONT_H);
    while (sc > 1 && text_width(label, sc) > b.w * 0.85f) sc--;
    text_at(r, label, b.x + (b.w - text_width(label, sc)) / 2, b.y + b.h / 2, sc, hexcol(0xFFFFFF));
}

void settings_draw(void *p, SDL_Renderer *r) {
    Settings *s = p;
    SDL_RenderSetViewport(r, NULL);
    vgradient(r, s->w, s->h, hexcol(0x0B3D5C), hexcol(0x10233A));
    int tsc = (int)(s->h * 0.06f / FONT_H);
    while (tsc > 1 && text_width("SETTINGS", tsc) > s->w * 0.9f) tsc--;
    text_at(r, "SETTINGS", (s->w - text_width("SETTINGS", tsc)) / 2,
            (int)(s->h * 0.08f), tsc, hexcol(0xFFE45C));

    static const char *const labels[ROWS] = { "SCREENSAVER", "START AFTER", "SCREEN OFF AFTER", "SHOW CLOCK", "CLOCK STYLE", "SAVE POWER" };
    int sc = (int)(s->rowH * 0.3f / FONT_H);
    if (sc < 1) sc = 1;
    while (sc > 1 && text_width("SCREEN OFF AFTER", sc) > s->w * 0.46f) sc--;
    char val[32];
    for (int i = 0; i < ROWS; i++) {
        SDL_Rect rw = s->row[i];
        fill_rrect(r, rw, rw.h / 4, i == s->hover ? hexcol(0x1D5578) : hexcol(0x163F5E));
        int cy = rw.y + rw.h / 2;
        /* Rows that don't matter right now, or can't work here, are drawn dimmed. */
        int dim = (i == R_CLOCK24 && !g_config.clock) || (i >= R_IDLE && i <= R_CLOCK24 && !g_config.enabled)
                  || (i <= R_OFF && !s->watcher);
        text_at(r, labels[i], rw.x + rw.h / 3, cy, sc, dim ? hexcol(0x7F93A6) : hexcol(0xFFFFFF));
        if (i == R_IDLE || i == R_OFF) {
            int v = i == R_IDLE ? g_config.idleMinutes : g_config.screenOffMinutes;
            if (v == 0) snprintf(val, sizeof val, "NEVER");
            else snprintf(val, sizeof val, "%d MIN", v);
            int lo = i == R_IDLE ? v > IDLE_OPTS[0] : v != 0;
            int hi = i == R_IDLE ? v < IDLE_OPTS[NOPTS - 1] : v < OFF_OPTS[NOPTS - 1];
            round_button(r, s->minus[i], 0, lo && !dim);
            round_button(r, s->plus[i], 1, hi && !dim);
            int mid = (s->minus[i].x + s->minus[i].w + s->plus[i].x) / 2;
            text_at(r, val, mid - text_width(val, sc) / 2, cy, sc, dim ? hexcol(0x7F93A6) : hexcol(0xFFFFFF));
        } else {
            int on = i == R_ENABLED ? g_config.enabled : i == R_CLOCK ? g_config.clock
                   : i == R_CLOCK24 ? g_config.clock24 : g_config.fps > 0;
            toggle_switch(r, s->toggle[i], on, !dim, s->t);
            const char *t = i == R_CLOCK24 ? (on ? "24 HOUR" : "12 HOUR")
                          : i == R_POWER ? (on ? "30 FPS" : "OFF") : (on ? "ON" : "OFF");
            text_at(r, t, s->toggle[i].x + s->toggle[i].w + rw.h / 3, cy, sc, dim ? hexcol(0x7F93A6) : hexcol(0xFFFFFF));
        }
    }
    button(r, s->test, "TEST IT", 0x43A047, 0x1B5E20);
    button(r, s->done, "BACK", 0x1E88E5, 0x0D4C8C);
    int nsc = sc * 2 / 3 > 1 ? sc * 2 / 3 : 1;
    const char *note = s->watcher ? "CHANGES ARE SAVED RIGHT AWAY"
                                  : "STARTING BY ITSELF WHEN IDLE WORKS ON LINUX ONLY";
    while (nsc > 1 && text_width(note, nsc) > s->w * 0.95f) nsc--;
    text_at(r, note, (s->w - text_width(note, nsc)) / 2, s->h - nsc * 8, nsc, hexcol(0x9FB6CC));
}

static int hit(SDL_Rect rc, int x, int y) { return x >= rc.x && y >= rc.y && x < rc.x + rc.w && y < rc.y + rc.h; }

static void click(Settings *s, SDL_Window *win, int x, int y) {
    if (hit(s->row[R_POWER], x, y)) { g_config.fps = g_config.fps > 0 ? 0 : 30; config_save(); return; }
    if (s->watcher && hit(s->row[R_ENABLED], x, y)) {   /* the master on/off switch */
        g_config.enabled = !g_config.enabled;
        s->timingChanged = 1;
        config_save();
        return;
    }
    if (g_config.enabled) {
        for (int i = R_IDLE; s->watcher && i <= R_OFF; i++) {
            if (hit(s->minus[i], x, y)) { stepper(i, -1); s->timingChanged = 1; config_save(); return; }
            if (hit(s->plus[i], x, y)) { stepper(i, +1); s->timingChanged = 1; config_save(); return; }
        }
        if (hit(s->row[R_CLOCK], x, y)) { g_config.clock = !g_config.clock; config_save(); return; }
        if (hit(s->row[R_CLOCK24], x, y) && g_config.clock) { g_config.clock24 = !g_config.clock24; config_save(); return; }
    }
    if (hit(s->test, x, y)) {
        if (run_saver(win, s->r, saver_resolve(g_config.saver), 0) == 1) s->quit = 1;
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        return;
    }
    if (hit(s->done, x, y)) s->quit = 1;
}

int run_settings(SDL_Window *win, SDL_Renderer *r) {
    int w, h, ww, wh;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_GetWindowSize(win, &ww, &wh);
    Settings *s = settings_create(r, w, h);
    float sx = (float)w / ww, sy = (float)h / wh;
    Uint64 last = SDL_GetPerformanceCounter();
    while (!s->quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) s->quit = 1;
            else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) s->quit = 1;
            else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT)
                click(s, win, (int)(e.button.x * sx), (int)(e.button.y * sy));
            else if (e.type == SDL_MOUSEMOTION) {
                int x = (int)(e.motion.x * sx), y = (int)(e.motion.y * sy);
                s->hover = -1;
                for (int i = 0; i < ROWS; i++) if (hit(s->row[i], x, y)) s->hover = i;
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                SDL_GetRendererOutputSize(r, &s->w, &s->h);
                SDL_GetWindowSize(win, &ww, &wh);
                sx = (float)s->w / ww;
                sy = (float)s->h / wh;
                layout(s);
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;
        settings_step(s, dt > 0.1f ? 0.1f : dt);
        settings_draw(s, r);
        SDL_RenderPresent(r);
        SDL_Delay(16);
    }
    /* New timings take effect now rather than at next login. */
    if (s->timingChanged && access(TOASTSAVER_BINDIR "/toastsaver-idle", X_OK) == 0)
        if (system(TOASTSAVER_BINDIR "/toastsaver-idle --restart >/dev/null 2>&1") != 0)
            fprintf(stderr, "toastsaver: could not restart the idle watcher\n");
    settings_destroy(s);
    return 0;
}

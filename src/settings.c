/* The settings page, reached from the gear in the picker's corner: on/off, timings,
   clock, power saving, a test run and a reset to the defaults.
   Every change is saved at once; timing changes restart the idle watcher when the page
   closes so they apply without logging out. The rows that need the idle watcher are
   greyed out where it isn't installed (the Mac preview), instead of doing nothing.
   The minutes rows step through a list with - and +, keep going past its end, and take
   any number typed in after tapping the value itself. */
#define _POSIX_C_SOURCE 200809L
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ROWS 7
static const int IDLE_OPTS[] = { 1, 2, 3, 5, 10, 15, 20, 30, 45, 60 };
static const int OFF_OPTS[] = { 0, 5, 10, 15, 20, 30, 45, 60, 90, 120 };
static const int CYCLE_OPTS[] = { 0, 2, 5, 10, 15, 20, 30, 45, 60, 120 };
#define NOPTS 10
#define PAST_END 30   /* past the end of a list, - and + move this many minutes */

typedef struct {
    SDL_Renderer *r;
    int w, h, quit, timingChanged, hover, watcher;
    int edit;              /* the minutes row being typed into, or -1 */
    char typed[5];
    float t, rowH, resetArm;   /* resetArm: seconds left to tap RESET again and confirm */
    SDL_Rect row[ROWS], minus[ROWS], plus[ROWS], field[ROWS], toggle[ROWS], test, reset, done;
} Settings;

enum { R_ENABLED, R_IDLE, R_OFF, R_CYCLE, R_CLOCK, R_CLOCK24, R_POWER };   /* row order */

static int is_minutes(int row) { return row >= R_IDLE && row <= R_CYCLE; }

static int *minutes_of(int row) {
    return row == R_IDLE ? &g_config.idleMinutes : row == R_OFF ? &g_config.screenOffMinutes
                                                                : &g_config.cycleMinutes;
}

static const int *opts_of(int row) { return row == R_IDLE ? IDLE_OPTS : row == R_OFF ? OFF_OPTS : CYCLE_OPTS; }

/* Can this row be changed right now? Timings need the idle watcher; all of them need ENABLED. */
static int live(const Settings *s, int row) {
    if (row == R_ENABLED) return s->watcher;
    if (row == R_POWER) return 1;
    if (!g_config.enabled) return 0;
    if (row == R_IDLE || row == R_OFF) return s->watcher;
    return row != R_CLOCK24 || g_config.clock;
}

static void layout(Settings *s) {
    float top = s->h * 0.14f, bottom = s->h * 0.82f;
    s->rowH = (bottom - top) / ROWS;
    int pad = (int)(s->rowH * 0.1f), btn = (int)(s->rowH * 0.62f);
    for (int i = 0; i < ROWS; i++) {
        SDL_Rect rw = { s->w / 20, (int)(top + i * s->rowH) + pad / 2, s->w - s->w / 10, (int)s->rowH - pad };
        s->row[i] = rw;
        int cy = rw.y + (rw.h - btn) / 2, right = rw.x + rw.w - pad * 2;
        s->plus[i] = (SDL_Rect){ right - btn, cy, btn, btn };
        s->minus[i] = (SDL_Rect){ (int)(s->w * 0.56f), cy, btn, btn };
        s->field[i] = (SDL_Rect){ s->minus[i].x + btn + pad, rw.y, s->plus[i].x - s->minus[i].x - btn - 2 * pad, rw.h };
        s->toggle[i] = (SDL_Rect){ (int)(s->w * 0.56f), cy, btn * 2, btn };
    }
    int bh = (int)(s->h * 0.11f), bw = s->w * 26 / 100, gap = s->w / 40, by = (int)(s->h * 0.85f);
    int x0 = (s->w - 3 * bw - 2 * gap) / 2;
    s->test = (SDL_Rect){ x0, by, bw, bh };
    s->reset = (SDL_Rect){ x0 + bw + gap, by, bw, bh };
    s->done = (SDL_Rect){ x0 + 2 * (bw + gap), by, bw, bh };
}

void *settings_create(SDL_Renderer *r, int w, int h) {
    Settings *s = calloc(1, sizeof *s);
    s->r = r;
    s->w = w;
    s->h = h;
    s->hover = -1;
    s->edit = -1;
    s->watcher = access(TOASTSAVER_BINDIR "/toastsaver-idle", X_OK) == 0;
    layout(s);
    return s;
}

void settings_destroy(void *p) { free(p); }

void settings_step(void *p, float dt) {
    Settings *s = p;
    s->t += dt;
    if (s->resetArm > 0) s->resetArm -= dt;
}

/* The next value up or down: through the list, then on past its end in PAST_END steps. */
static int step_value(const int *opts, int v, int dir) {
    int last = opts[NOPTS - 1];
    if (dir > 0) {
        if (v >= last) return v + PAST_END <= CONFIG_MAX_MINUTES ? v + PAST_END : v;
        for (int i = 0; i < NOPTS; i++) if (opts[i] > v) return opts[i];
    } else {
        if (v > last) return v - PAST_END > last ? v - PAST_END : last;
        for (int i = NOPTS - 1; i >= 0; i--) if (opts[i] < v) return opts[i];
    }
    return v;
}

/* Keep "screen off" later than "screensaver after" (or never). */
static void fix_order(void) {
    int off = g_config.screenOffMinutes, idle = g_config.idleMinutes;
    if (off != 0 && off <= idle) g_config.screenOffMinutes = step_value(OFF_OPTS, idle, +1);
}

static void stepper(int row, int dir) {
    int *v = minutes_of(row), nv = step_value(opts_of(row), *v, dir);
    while (row == R_OFF && nv != 0 && nv <= g_config.idleMinutes) {   /* skip too-early values */
        int next = step_value(OFF_OPTS, nv, dir);
        if (next == nv) return;
        nv = next;
    }
    *v = nv;
    fix_order();
}

/* ---------- typing a number ---------- */

static void start_edit(Settings *s, int row) {
    s->edit = row;
    s->typed[0] = 0;
    SDL_StartTextInput();
}

/* Enter, or a tap anywhere else: keep what was typed (nothing typed changes nothing). */
static void end_edit(Settings *s, int keep) {
    int row = s->edit;
    s->edit = -1;
    SDL_StopTextInput();
    if (!keep || !s->typed[0]) return;
    int n = atoi(s->typed);
    if (row == R_IDLE && n < 1) n = 1;
    *minutes_of(row) = n;
    fix_order();
    if (row != R_CYCLE) s->timingChanged = 1;
    config_save();
}

static void type_text(Settings *s, const char *text) {
    for (; *text; text++) {
        size_t n = strlen(s->typed);
        if (*text >= '0' && *text <= '9' && n < sizeof s->typed - 1) {
            if (n == 1 && s->typed[0] == '0') n = 0;    /* no leading zeros */
            s->typed[n] = *text;
            s->typed[n + 1] = 0;
        }
    }
}

/* ---------- drawing ---------- */

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

/* The value between - and +: the minutes, NEVER, or the number being typed in a white box. */
static void minutes_value(const Settings *s, SDL_Renderer *r, int row, int sc, int dim) {
    SDL_Rect f = s->field[row];
    int cy = f.y + f.h / 2, v = *minutes_of(row);
    char val[24];
    if (s->edit == row) {
        SDL_Rect box = { f.x, cy - FONT_H * sc, f.w, FONT_H * sc * 2 };
        fill_rrect(r, box, box.h / 4, hexcol(0xFFFFFF));
        snprintf(val, sizeof val, "%s MIN", s->typed);
        int x = f.x + (f.w - text_width(val, sc)) / 2;
        draw_text(r, val, x, cy - FONT_H * sc / 2, sc, hexcol(0x10233A));
        if ((int)(s->t * 2) % 2 == 0) {                      /* a blinking caret after the digits */
            SDL_Rect caret = { x + text_width(s->typed, sc) + sc / 2, cy - FONT_H * sc / 2, sc, FONT_H * sc };
            SDL_SetRenderDrawColor(r, 0x1E, 0x88, 0xE5, 255);
            SDL_RenderFillRect(r, &caret);
        }
        return;
    }
    if (v == 0) snprintf(val, sizeof val, "NEVER");
    else snprintf(val, sizeof val, "%d MIN", v);
    text_at(r, val, f.x + (f.w - text_width(val, sc)) / 2, cy, sc, dim ? hexcol(0x7F93A6) : hexcol(0xFFFFFF));
}

void settings_draw(void *p, SDL_Renderer *r) {
    Settings *s = p;
    SDL_RenderSetViewport(r, NULL);
    vgradient(r, s->w, s->h, hexcol(0x0B3D5C), hexcol(0x10233A));
    int tsc = (int)(s->h * 0.06f / FONT_H);
    while (tsc > 1 && text_width("SETTINGS", tsc) > s->w * 0.9f) tsc--;
    text_at(r, "SETTINGS", (s->w - text_width("SETTINGS", tsc)) / 2,
            (int)(s->h * 0.07f), tsc, hexcol(0xFFE45C));

    static const char *const labels[ROWS] = { "SCREENSAVER", "START AFTER", "SCREEN OFF AFTER", "SURPRISE SWITCH",
                                              "SHOW CLOCK", "CLOCK STYLE", "SAVE POWER" };
    int sc = (int)(s->rowH * 0.3f / FONT_H);
    if (sc < 1) sc = 1;
    while (sc > 1 && text_width("SCREEN OFF AFTER", sc) > s->w * 0.46f) sc--;
    for (int i = 0; i < ROWS; i++) {
        SDL_Rect rw = s->row[i];
        fill_rrect(r, rw, rw.h / 4, i == s->hover ? hexcol(0x1D5578) : hexcol(0x163F5E));
        int cy = rw.y + rw.h / 2;
        /* Rows that don't matter right now, or can't work here, are drawn dimmed. */
        int dim = !live(s, i);
        text_at(r, labels[i], rw.x + rw.h / 3, cy, sc, dim ? hexcol(0x7F93A6) : hexcol(0xFFFFFF));
        if (is_minutes(i)) {
            int v = *minutes_of(i);
            int lo = i == R_IDLE ? v > 1 : v != 0;
            int hi = v + PAST_END <= CONFIG_MAX_MINUTES;
            round_button(r, s->minus[i], 0, lo && !dim);
            round_button(r, s->plus[i], 1, hi && !dim);
            minutes_value(s, r, i, sc, dim);
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
    if (s->resetArm > 0) button(r, s->reset, "SURE?", 0xE53935, 0x8E1B18);
    else button(r, s->reset, "RESET", 0x78909C, 0x455A64);
    button(r, s->done, "BACK", 0x1E88E5, 0x0D4C8C);
    int nsc = sc * 2 / 3 > 1 ? sc * 2 / 3 : 1;
    const char *note = s->edit >= 0 ? "TYPE THE MINUTES AND PRESS ENTER"
                     : s->resetArm > 0 ? "TAP SURE? TO PUT EVERY SETTING HERE BACK"
                     : s->watcher ? "TAP A TIME TO TYPE ONE IN. CHANGES ARE SAVED RIGHT AWAY"
                     : "STARTING BY ITSELF WHEN IDLE WORKS ON LINUX ONLY";
    while (nsc > 1 && text_width(note, nsc) > s->w * 0.95f) nsc--;
    text_at(r, note, (s->w - text_width(note, nsc)) / 2, s->h - nsc * 6, nsc, hexcol(0x9FB6CC));
}

/* ---------- input ---------- */

static int hit(SDL_Rect rc, int x, int y) { return x >= rc.x && y >= rc.y && x < rc.x + rc.w && y < rc.y + rc.h; }

/* Everything on this page back to how a new install starts (the saver choice and name stay). */
static void reset_defaults(Settings *s) {
    const Config *d = &CONFIG_DEFAULTS;
    g_config.enabled = d->enabled;
    g_config.idleMinutes = d->idleMinutes;
    g_config.screenOffMinutes = d->screenOffMinutes;
    g_config.cycleMinutes = d->cycleMinutes;
    g_config.clock = d->clock;
    g_config.clock24 = d->clock24;
    g_config.fps = d->fps;
    s->timingChanged = 1;
    config_save();
}

static void click(Settings *s, SDL_Window *win, int x, int y) {
    if (s->edit >= 0) {
        int same = hit(s->field[s->edit], x, y);
        end_edit(s, 1);
        if (same) return;                               /* tapping the box again just finishes */
    }
    if (hit(s->reset, x, y)) {
        if (s->resetArm > 0) { s->resetArm = 0; reset_defaults(s); }
        else s->resetArm = 3;
        return;
    }
    s->resetArm = 0;
    if (hit(s->row[R_POWER], x, y)) { g_config.fps = g_config.fps > 0 ? 0 : 30; config_save(); return; }
    if (live(s, R_ENABLED) && hit(s->row[R_ENABLED], x, y)) {   /* the master on/off switch */
        g_config.enabled = !g_config.enabled;
        s->timingChanged = 1;
        config_save();
        return;
    }
    for (int i = R_IDLE; i <= R_CYCLE; i++) {
        if (!live(s, i)) continue;
        int dir = hit(s->minus[i], x, y) ? -1 : hit(s->plus[i], x, y) ? 1 : 0;
        if (dir) {
            stepper(i, dir);
            if (i != R_CYCLE) s->timingChanged = 1;
            config_save();
            return;
        }
        if (hit(s->field[i], x, y)) { start_edit(s, i); return; }
    }
    if (live(s, R_CLOCK) && hit(s->row[R_CLOCK], x, y)) { g_config.clock = !g_config.clock; config_save(); return; }
    if (live(s, R_CLOCK24) && hit(s->row[R_CLOCK24], x, y)) { g_config.clock24 = !g_config.clock24; config_save(); return; }
    if (hit(s->test, x, y)) {
        if (run_saver(win, s->r, saver_resolve(g_config.saver), 0, !strcmp(g_config.saver, "random")) == 1) s->quit = 1;
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        return;
    }
    if (hit(s->done, x, y)) s->quit = 1;
}

static void key(Settings *s, SDL_Keycode k) {
    if (s->edit < 0) { if (k == SDLK_ESCAPE) s->quit = 1; return; }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) end_edit(s, 1);
    else if (k == SDLK_ESCAPE) end_edit(s, 0);
    else if (k == SDLK_BACKSPACE && s->typed[0]) s->typed[strlen(s->typed) - 1] = 0;
}

int run_settings(SDL_Window *win, SDL_Renderer *r) {
    int w, h, ww, wh;
    SDL_GetRendererOutputSize(r, &w, &h);
    SDL_GetWindowSize(win, &ww, &wh);
    Settings *s = settings_create(r, w, h);
    float sx = (float)w / ww, sy = (float)h / wh;
    Uint64 last = SDL_GetPerformanceCounter();
    SDL_StopTextInput();
    while (!s->quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) s->quit = 1;
            else if (e.type == SDL_KEYDOWN) key(s, e.key.keysym.sym);
            else if (e.type == SDL_TEXTINPUT && s->edit >= 0) type_text(s, e.text.text);
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
    if (s->edit >= 0) end_edit(s, 1);
    /* New timings take effect now rather than at next login. */
    if (s->timingChanged && access(TOASTSAVER_BINDIR "/toastsaver-idle", X_OK) == 0)
        if (system(TOASTSAVER_BINDIR "/toastsaver-idle --restart >/dev/null 2>&1") != 0)
            fprintf(stderr, "toastsaver: could not restart the idle watcher\n");
    settings_destroy(s);
    return 0;
}

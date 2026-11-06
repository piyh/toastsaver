/* Settings pages and the windowed-mode panel, both drawn from tables of SaverOption rows.
     - A page (run_page) shows a table under its headings: - and + (or a switch) on each row,
       a number you can tap to type in, rows that open another page, and TEST IT, RESET (tap
       twice) and BACK. It scrolls (wheel, drag, arrows) when the rows don't fit. Every change
       is saved at once. The main settings page is one (settings.c); so are the list of savers
       with settings of their own and each saver's page (run_saver_options).
     - The panel in windowed mode (O opens it) is for trying values while the saver runs: up
       and down pick a row, left and right change it, Enter (or SAVE) writes the settings file,
       and DEFAULTS puts them back. A change to a setting the saver only reads when it starts
       (OPT_RESTART) starts the saver over. */
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- pages ---------- */

#define ROW_MAX 64

typedef struct {
    SDL_Renderer *r;
    SDL_Window *win;
    const PageSpec *spec;
    PageSpec own;                       /* the spec of a page made here (a saver's, or the list) */
    SaverOption ownRows[ROW_MAX + 1];
    const Saver *owner;                 /* a saver's page: RESET puts its settings back */
    char title[64];
    int w, h, n, quit, hover, edit;     /* edit: the row being typed into, or -1. quit: 1 back, 2 close the app */
    char typed[6];
    float t, rowH, resetArm, scroll, scrollMax;
    int sc;                             /* text size on the rows */
    int top, bottom;                    /* the rows' area on screen; rows outside it scroll */
    int pressed, dragging, pressX, pressY;
    float pressScroll;
    /* rows in content coordinates: on screen they are `scroll` higher */
    SDL_Rect head[ROW_MAX], row[ROW_MAX], minus[ROW_MAX], plus[ROW_MAX], field[ROW_MAX], toggle[ROW_MAX];
    SDL_Rect test, reset, done;
} Page;

static const SaverOption *row_of(const Page *p, int i) { return &p->spec->rows[i]; }

static int dim(const SaverOption *o) { return o->live && !o->live(); }

static SDL_Rect on_screen(const Page *p, SDL_Rect rc) {
    rc.y -= (int)p->scroll;
    return rc;
}

static int hit_row(const Page *p, SDL_Rect rc, int x, int y) {
    return y >= p->top && y < p->bottom && ui_hit(on_screen(p, rc), x, y);
}

static void clamp_scroll(Page *p) {
    if (p->scroll > p->scrollMax) p->scroll = p->scrollMax;
    if (p->scroll < 0) p->scroll = 0;
}

/* Rows go in one column, or in two side by side when the page is wide and has many rows: whole
   groups go to each column, split where the two come out most even. A row has its label on the
   left and a compact cluster on the right: - value +, or a switch and its word. */
static void page_layout(Page *p) {
    const float headH = 0.6f;
    p->top = (int)(p->h * 0.13f);
    p->bottom = (int)(p->h * 0.83f);
    float area = (float)(p->bottom - p->top), units[ROW_MAX], total = 0;
    for (int i = 0; i < p->n; i++) total += units[i] = 1 + (row_of(p, i)->group ? headH : 0);
    int split = p->n;                                     /* the first row of the second column */
    float colUnits = total;
    if (p->n > 8 && p->w >= p->h * 1.2f) {
        float a = 0;
        for (int i = 0; i < p->n; i++) {
            if (i > 0 && row_of(p, i)->group && (a > total - a ? a : total - a) < colUnits) {
                colUnits = a > total - a ? a : total - a;
                split = i;
            }
            a += units[i];
        }
    }
    p->rowH = area / (colUnits + 0.001f);
    if (p->rowH < p->h * 0.058f) p->rowH = p->h * 0.058f;    /* any smaller is hard to tap: scroll instead */
    if (p->rowH > p->h * 0.09f) p->rowH = p->h * 0.09f;
    int cols = split < p->n ? 2 : 1, gap = p->w / 30, x0 = p->w / 20, fullW = p->w - p->w / 10;
    int cw = cols == 2 ? (fullW - gap) / 2 : fullW;
    int pad = (int)(p->rowH * 0.1f), btn = (int)(p->rowH * 0.62f);

    /* the text size, and the controls' width at that size; smaller text until every label fits */
    int sc = (int)(p->rowH * 0.3f / FONT_H), ctrlW = 0;
    if (sc < 1) sc = 1;
    for (;; sc--) {
        int fw = text_width("9999 WPM", sc) + 2 * pad, word = text_width("24 HOUR", sc);
        if (fw < btn * 2) fw = btn * 2;
        ctrlW = 2 * btn + fw + 2 * pad;
        if (btn * 2 + pad * 2 + word > ctrlW) ctrlW = btn * 2 + pad * 2 + word;
        int fits = 1;
        for (int i = 0; i < p->n; i++)
            if (text_width(row_of(p, i)->label, sc) > cw - ctrlW - (int)p->rowH) fits = 0;
        if (fits || sc == 1) break;
    }
    p->sc = sc;

    float y[2] = { (float)p->top, (float)p->top };
    for (int i = 0; i < p->n; i++) {
        int c = i >= split, x = x0 + c * (cw + gap);
        p->head[i] = (SDL_Rect){ x, (int)y[c], cw, 0 };
        if (row_of(p, i)->group) {
            p->head[i].h = (int)(p->rowH * headH);
            y[c] += p->rowH * headH;
        }
        SDL_Rect r = { x, (int)y[c] + pad / 2, cw, (int)p->rowH - pad };
        y[c] += p->rowH;
        p->row[i] = r;
        int cy = r.y + (r.h - btn) / 2, right = r.x + r.w - pad * 2, cx = right - ctrlW;
        p->minus[i] = (SDL_Rect){ cx, cy, btn, btn };
        p->plus[i] = (SDL_Rect){ right - btn, cy, btn, btn };
        p->field[i] = (SDL_Rect){ cx + btn + pad, r.y, ctrlW - 2 * btn - 2 * pad, r.h };
        p->toggle[i] = (SDL_Rect){ cx, cy, btn * 2, btn };
    }
    float end = y[0] > y[1] ? y[0] : y[1];
    p->scrollMax = end - p->bottom > 0 ? end - p->bottom : 0;
    clamp_scroll(p);
    int bh = (int)(p->h * 0.11f), bw = p->w * 26 / 100, bgap = p->w / 40, by = (int)(p->h * 0.855f);
    int bx = (p->w - 3 * bw - 2 * bgap) / 2;
    p->test = (SDL_Rect){ bx, by, bw, bh };
    p->reset = (SDL_Rect){ bx + bw + bgap, by, bw, bh };
    p->done = (SDL_Rect){ p->spec->buttons == 1 ? (p->w - bw) / 2 : bx + 2 * (bw + bgap), by, bw, bh };
}

static Page *page_new(SDL_Renderer *r, int w, int h) {
    Page *p = calloc(1, sizeof *p);
    p->r = r;
    p->w = w;
    p->h = h;
    p->hover = p->edit = -1;
    return p;
}

static void page_start(Page *p, const PageSpec *spec) {
    p->spec = spec;
    p->n = 0;
    while (p->n < ROW_MAX && spec->rows[p->n].label) p->n++;
    page_layout(p);
}

void *page_create(SDL_Renderer *r, int w, int h, const PageSpec *spec) {
    Page *p = page_new(r, w, h);
    page_start(p, spec);
    return p;
}

/* A saver's own page, or (sv = NULL) the list of savers that have settings, each opening its page. */
void *options_page_create(SDL_Renderer *r, int w, int h, const Saver *sv) {
    Page *p = page_new(r, w, h);
    snprintf(p->title, sizeof p->title, "%s SETTINGS", sv ? sv->label : "SAVER");
    p->own.title = p->title;
    if (sv) {
        p->owner = sv;
        p->own.rows = sv->options;
        p->own.test = sv;
        p->own.resetNote = "TAP SURE? TO PUT THIS SAVER'S SETTINGS BACK";
    } else {
        int n = 0;
        for (int i = 0; i < SAVER_COUNT && n < ROW_MAX; i++)
            if (option_count(SAVERS[i]))
                p->ownRows[n++] = (SaverOption){ .label = SAVERS[i]->label, .flags = OPT_LINK, .link = SAVERS[i] };
        p->own.rows = p->ownRows;
        p->own.buttons = 1;
    }
    page_start(p, &p->own);
    return p;
}

void page_destroy(void *p) { free(p); }

void page_step(void *pp, float dt) {
    Page *p = pp;
    p->t += dt;
    if (p->resetArm > 0) p->resetArm -= dt;
}

/* ---------- drawing ---------- */

/* What a link row says on its right: how many settings a saver has, or which savers have some. */
static void link_text(const SaverOption *o, char *out, size_t n) {
    if (o->link) {
        int k = option_count(o->link);
        snprintf(out, n, "%d SETTING%s  >", k, k == 1 ? "" : "S");
        return;
    }
    size_t len = 0;
    out[0] = 0;
    for (int k = 0; k < SAVER_COUNT && len + 24 < n; k++)
        if (option_count(SAVERS[k]))
            len += (size_t)snprintf(out + len, n - len, "%s%s", len ? ", " : "", SAVERS[k]->label);
    snprintf(out + len, n - len, "  >");
}

static void draw_row(const Page *p, SDL_Renderer *r, int i, int sc) {
    const SaverOption *o = row_of(p, i);
    SDL_Rect rw = on_screen(p, p->row[i]);
    int cy = rw.y + rw.h / 2, off = dim(o);
    SDL_Color text = off ? hexcol(0x7F93A6) : hexcol(0xFFFFFF);
    fill_rrect(r, rw, rw.h / 4, i == p->hover && !off ? hexcol(0x1D5578) : hexcol(0x163F5E));
    ui_text_at(r, o->label, rw.x + rw.h / 3, cy, sc, text);
    SDL_Rect tg = on_screen(p, p->toggle[i]);
    char val[160];
    if (o->flags & OPT_LINK) {
        link_text(o, val, sizeof val);
        int lsc = sc;
        while (lsc > 1 && text_width(val, lsc) > rw.x + rw.w - tg.x - rw.h / 3) lsc--;
        ui_text_at(r, val, tg.x, cy, lsc, text);
        return;
    }
    int v = *o->value;
    if (o->flags & OPT_TOGGLE) {
        ui_toggle(r, tg, v != 0, !off);
        option_format(o, v, val, sizeof val);
        ui_text_at(r, val, tg.x + tg.w + rw.h / 3, cy, sc, text);
        return;
    }
    ui_round_button(r, on_screen(p, p->minus[i]), 0, !off && option_step(o, v, -1) != v);
    ui_round_button(r, on_screen(p, p->plus[i]), 1, !off && option_step(o, v, 1) != v);
    SDL_Rect f = on_screen(p, p->field[i]);
    if (p->edit == i) {                                          /* typing: a white box with a blinking caret */
        SDL_Rect box = { f.x, cy - FONT_H * sc, f.w, FONT_H * sc * 2 };
        fill_rrect(r, box, box.h / 4, hexcol(0xFFFFFF));
        snprintf(val, sizeof val, "%s%s%s", p->typed, o->unit ? " " : "", o->unit ? o->unit : "");
        int x = f.x + (f.w - text_width(val, sc)) / 2;
        draw_text(r, val, x, cy - FONT_H * sc / 2, sc, hexcol(0x10233A));
        if ((int)(p->t * 2) % 2 == 0) {
            SDL_Rect caret = { x + text_width(p->typed, sc) + sc / 2, cy - FONT_H * sc / 2, sc, FONT_H * sc };
            SDL_SetRenderDrawColor(r, 0x1E, 0x88, 0xE5, 255);
            SDL_RenderFillRect(r, &caret);
        }
        return;
    }
    option_format(o, v, val, sizeof val);
    ui_text_at(r, val, f.x + (f.w - text_width(val, sc)) / 2, cy, sc, text);
}

void page_draw(void *pp, SDL_Renderer *r) {
    Page *p = pp;
    const PageSpec *sp = p->spec;
    SDL_RenderSetViewport(r, NULL);
    vgradient(r, p->w, p->h, hexcol(0x0B3D5C), hexcol(0x10233A));
    int tsc = (int)(p->h * 0.06f / FONT_H);
    while (tsc > 1 && text_width(sp->title, tsc) > p->w * 0.9f) tsc--;
    ui_text_at(r, sp->title, (p->w - text_width(sp->title, tsc)) / 2, (int)(p->h * 0.065f), tsc, hexcol(0xFFE45C));

    int sc = p->sc;
    SDL_Rect clip = { 0, p->top, p->w, p->bottom - p->top };
    SDL_RenderSetClipRect(r, &clip);
    for (int i = 0; i < p->n; i++) {
        SDL_Rect hd = on_screen(p, p->head[i]), rw = on_screen(p, p->row[i]);
        if (hd.h && hd.y + hd.h > p->top && hd.y < p->bottom)
            ui_text_at(r, row_of(p, i)->group, hd.x + (int)(p->rowH / 6), hd.y + hd.h * 6 / 10, sc, hexcol(0x9FB6CC));
        if (rw.y + rw.h > p->top && rw.y < p->bottom) draw_row(p, r, i, sc);
    }
    SDL_RenderSetClipRect(r, NULL);
    if (p->scrollMax > 0) {                                      /* a scroll bar, and a fade where rows go on */
        float span = (float)(p->bottom - p->top), all = span + p->scrollMax;
        SDL_Rect track = { p->w - p->w / 40, p->top, p->w / 160 > 3 ? p->w / 160 : 3, (int)span };
        SDL_Rect knob = { track.x, p->top + (int)(span * p->scroll / all), track.w, (int)(span * span / all) };
        fill_rrect(r, track, track.w / 2, hexcol(0x163F5E));
        fill_rrect(r, knob, track.w / 2, hexcol(0x9FB6CC));
    }
    if (sp->buttons != 1) {
        ui_button(r, p->test, "TEST IT", 0x43A047, 0x1B5E20);
        if (p->resetArm > 0) ui_button(r, p->reset, "SURE?", 0xE53935, 0x8E1B18);
        else ui_button(r, p->reset, "RESET", 0x78909C, 0x455A64);
    }
    ui_button(r, p->done, "BACK", 0x1E88E5, 0x0D4C8C);
    const char *note = p->edit >= 0 ? "TYPE A NUMBER AND PRESS ENTER"
                     : p->resetArm > 0 && sp->resetNote ? sp->resetNote
                     : sp->note ? sp->note()
                     : sp->buttons == 1 ? "TAP ONE TO CHANGE ITS SETTINGS"
                     : p->scrollMax > 0 ? "SCROLL FOR MORE. TAP A NUMBER TO TYPE ONE IN. CHANGES ARE SAVED RIGHT AWAY"
                     : "TAP A NUMBER TO TYPE ONE IN. CHANGES ARE SAVED RIGHT AWAY";
    int nsc = sc * 2 / 3 > 1 ? sc * 2 / 3 : 1;
    while (nsc > 1 && text_width(note, nsc) > p->w * 0.95f) nsc--;
    ui_text_at(r, note, (p->w - text_width(note, nsc)) / 2, p->h - nsc * 6, nsc, hexcol(0x9FB6CC));
}

/* ---------- input ---------- */

static void changed(Page *p, const SaverOption *o, int old, int dir) {
    if (o->changed) o->changed(o, old, dir);
    (void)p;
    config_save();
}

static void end_edit(Page *p, int keep) {
    int row = p->edit;
    p->edit = -1;
    SDL_StopTextInput();
    if (!keep || !p->typed[0]) return;
    const SaverOption *o = row_of(p, row);
    int old = *o->value;
    option_set(o, atoi(p->typed));
    changed(p, o, old, 0);
}

static void relayout(Page *p) {
    SDL_GetRendererOutputSize(p->r, &p->w, &p->h);
    page_layout(p);
}

static void click(Page *p, int x, int y) {
    if (p->edit >= 0) {
        int same = hit_row(p, p->field[p->edit], x, y);
        end_edit(p, 1);
        if (same) return;
    }
    if (ui_hit(p->done, x, y)) { p->quit = 1; return; }
    if (p->spec->buttons != 1 && ui_hit(p->reset, x, y)) {
        if (p->resetArm > 0) {
            p->resetArm = 0;
            if (p->spec->reset) p->spec->reset();
            if (p->owner) option_reset(p->owner);
            config_save();
        } else p->resetArm = 3;
        return;
    }
    p->resetArm = 0;
    if (p->spec->buttons != 1 && ui_hit(p->test, x, y)) {
        const char *id = g_config.saver;
        const Saver *sv = p->spec->test ? p->spec->test : saver_resolve(id);
        if (run_saver(p->win, p->r, sv, 0, !p->spec->test && !strcmp(id, "random")) == 1) p->quit = 2;
        SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
        return;
    }
    for (int i = 0; i < p->n; i++) {
        const SaverOption *o = row_of(p, i);
        if (dim(o)) continue;
        if (o->flags & OPT_LINK) {
            if (!hit_row(p, p->row[i], x, y)) continue;
            if (run_saver_options(p->win, p->r, o->link) == 1) p->quit = 2;
            SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
            relayout(p);
            return;
        }
        int old = *o->value;
        if (o->flags & OPT_TOGGLE) {
            if (!hit_row(p, p->row[i], x, y)) continue;
            *o->value = old ? 0 : option_on(o);
            changed(p, o, old, old ? -1 : 1);
            return;
        }
        int dir = hit_row(p, p->minus[i], x, y) ? -1 : hit_row(p, p->plus[i], x, y) ? 1 : 0;
        if (dir) {
            *o->value = option_step(o, old, dir);
            if (*o->value != old) changed(p, o, old, dir);
            return;
        }
        if (hit_row(p, p->field[i], x, y)) {
            p->edit = i;
            p->typed[0] = 0;
            SDL_StartTextInput();
            return;
        }
    }
}

static void key(Page *p, SDL_Keycode k) {
    if (p->edit < 0) {
        if (k == SDLK_ESCAPE) p->quit = 1;
        else if (k == SDLK_UP || k == SDLK_DOWN) p->scroll += (k == SDLK_UP ? -1 : 1) * p->rowH;
        else if (k == SDLK_PAGEUP || k == SDLK_PAGEDOWN) p->scroll += (k == SDLK_PAGEUP ? -1 : 1) * (p->bottom - p->top) * 0.9f;
        clamp_scroll(p);
        return;
    }
    if (k == SDLK_RETURN || k == SDLK_KP_ENTER) end_edit(p, 1);
    else if (k == SDLK_ESCAPE) end_edit(p, 0);
    else if (k == SDLK_BACKSPACE && p->typed[0]) p->typed[strlen(p->typed) - 1] = 0;
}

static void type_text(Page *p, const char *text) {
    for (; *text; text++) {
        size_t n = strlen(p->typed);
        if (*text >= '0' && *text <= '9' && n < sizeof p->typed - 1) {
            if (n == 1 && p->typed[0] == '0') n = 0;    /* no leading zeros */
            p->typed[n] = *text;
            p->typed[n + 1] = 0;
        }
    }
}

/* Runs a page until BACK, Esc or the window closes. A press acts when it is let go, unless
   it turned into a drag, which scrolls. Returns 1 to quit the app. */
static int page_run(SDL_Window *win, Page *p) {
    int ww, wh;
    p->win = win;
    SDL_GetWindowSize(win, &ww, &wh);
    float sx = (float)p->w / ww, sy = (float)p->h / wh;
    Uint64 last = SDL_GetPerformanceCounter();
    SDL_StopTextInput();
    while (!p->quit) {
        SDL_Event e;
        while (!p->quit && SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) p->quit = 2;
            else if (e.type == SDL_KEYDOWN) key(p, e.key.keysym.sym);
            else if (e.type == SDL_TEXTINPUT && p->edit >= 0) type_text(p, e.text.text);
            else if (e.type == SDL_MOUSEWHEEL) {
                p->scroll -= e.wheel.y * p->rowH;
                clamp_scroll(p);
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                p->pressed = 1;
                p->dragging = 0;
                p->pressX = (int)(e.button.x * sx);
                p->pressY = (int)(e.button.y * sy);
                p->pressScroll = p->scroll;
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                if (p->pressed && !p->dragging) {
                    click(p, p->pressX, p->pressY);
                    SDL_GetWindowSize(win, &ww, &wh);       /* a page opened from here may have seen a resize */
                    sx = (float)p->w / ww;
                    sy = (float)p->h / wh;
                    last = SDL_GetPerformanceCounter();
                }
                p->pressed = 0;
            } else if (e.type == SDL_MOUSEMOTION) {
                int x = (int)(e.motion.x * sx), y = (int)(e.motion.y * sy);
                if (p->pressed && p->scrollMax > 0 && abs(y - p->pressY) > p->h / 60) p->dragging = 1;
                if (p->dragging) {
                    p->scroll = p->pressScroll - (y - p->pressY);
                    clamp_scroll(p);
                }
                p->hover = -1;
                for (int i = 0; i < p->n; i++) if (hit_row(p, p->row[i], x, y)) p->hover = i;
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                relayout(p);
                SDL_GetWindowSize(win, &ww, &wh);
                sx = (float)p->w / ww;
                sy = (float)p->h / wh;
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;
        page_step(p, dt > 0.1f ? 0.1f : dt);
        page_draw(p, p->r);
        SDL_RenderPresent(p->r);
        SDL_Delay(16);
    }
    if (p->edit >= 0) end_edit(p, 1);
    int quit = p->quit == 2;
    page_destroy(p);
    return quit;
}

int run_page(SDL_Window *win, SDL_Renderer *r, const PageSpec *spec) {
    int w, h;
    SDL_GetRendererOutputSize(r, &w, &h);
    return page_run(win, page_create(r, w, h, spec));
}

int run_saver_options(SDL_Window *win, SDL_Renderer *r, const Saver *sv) {
    int w, h;
    SDL_GetRendererOutputSize(r, &w, &h);
    return page_run(win, options_page_create(r, w, h, sv));
}

/* ---------- the windowed-mode panel ---------- */

void option_panel_init(OptionPanel *p, const Saver *sv) {
    memset(p, 0, sizeof *p);
    for (int i = 0, n = option_count(sv); i < n; i++) p->saved[i] = *sv->options[i].value;
}

static void panel_save(OptionPanel *p, const Saver *sv) {
    if (config_save() != 0) { fprintf(stderr, "toastsaver: cannot write %s\n", config_path()); return; }
    for (int i = 0, n = option_count(sv); i < n; i++) p->saved[i] = *sv->options[i].value;
    p->savedFlash = 1.5f;
    printf("saved %s settings to %s\n", sv->id, config_path());
    fflush(stdout);
}

/* Sets option i, and says whether the saver must start over for it to show. */
static int panel_change(const Saver *sv, int i, int v) {
    const SaverOption *o = &sv->options[i];
    int old = *o->value;
    option_set(o, v);
    return *o->value != old && (o->flags & OPT_RESTART);
}

static int panel_defaults(const Saver *sv) {
    int restart = 0;
    for (int i = 0, n = option_count(sv); i < n; i++) restart |= panel_change(sv, i, sv->options[i].def);
    return restart;
}

int option_panel_key(OptionPanel *p, const Saver *sv, SDL_Keycode k) {
    int n = option_count(sv);
    if (!n) return -1;
    if (k == SDLK_o) { p->open = !p->open; return 0; }
    if (!p->open) return -1;
    if (k == SDLK_ESCAPE) { p->open = 0; return 0; }   /* Esc closes the panel before the window */
    const SaverOption *o = &sv->options[p->sel];
    switch (k) {
    case SDLK_UP: p->sel = (p->sel + n - 1) % n; return 0;
    case SDLK_DOWN: case SDLK_TAB: p->sel = (p->sel + 1) % n; return 0;
    case SDLK_LEFT: return panel_change(sv, p->sel, option_step(o, *o->value, -1));
    case SDLK_RIGHT: return panel_change(sv, p->sel, option_step(o, *o->value, 1));
    case SDLK_RETURN: case SDLK_KP_ENTER: panel_save(p, sv); return 0;
    case SDLK_BACKSPACE: case SDLK_DELETE: return panel_defaults(sv);
    default: return -1;
    }
}

int option_panel_click(OptionPanel *p, const Saver *sv, int x, int y) {
    if (!p->open || !ui_hit(p->box, x, y)) return -1;
    for (int i = 0, n = option_count(sv); i < n; i++) {
        const SaverOption *o = &sv->options[i];
        if (ui_hit(p->minus[i], x, y)) { p->sel = i; return panel_change(sv, i, option_step(o, *o->value, -1)); }
        if (ui_hit(p->plus[i], x, y)) { p->sel = i; return panel_change(sv, i, option_step(o, *o->value, 1)); }
        if (ui_hit(p->row[i], x, y)) {
            p->sel = i;
            return (o->flags & OPT_TOGGLE) ? panel_change(sv, i, *o->value ? 0 : option_on(o)) : 0;
        }
    }
    if (ui_hit(p->save, x, y)) panel_save(p, sv);
    else if (ui_hit(p->defaults, x, y)) return panel_defaults(sv);
    return 0;                                              /* inside the panel: not for anything under it */
}

static void small_button(SDL_Renderer *r, SDL_Rect b, const char *label, int sc, int lit) {
    SDL_SetRenderDrawColor(r, 255, 255, 255, lit ? 90 : 40);
    SDL_RenderFillRect(r, &b);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 200);
    SDL_RenderDrawRect(r, &b);
    draw_text_shadow(r, label, b.x + (b.w - text_width(label, sc)) / 2, b.y + (b.h - FONT_H * sc) / 2, sc,
                     (SDL_Color){ 255, 255, 255, 255 });
}

void option_panel_draw(OptionPanel *p, const Saver *sv, SDL_Renderer *r, int w, int h, float dt) {
    int n = option_count(sv);
    if (p->savedFlash > 0) p->savedFlash -= dt;
    if (!p->open || !n) return;
    int sc = h / 300 > 2 ? h / 300 : 2;
    while (sc > 1 && 11 * sc * (n + 3) + 9 * sc + 40 > h) sc--;    /* a short window: smaller, so every row fits */
    int rh = 11 * sc, pad = 3 * sc;
    int labelW = 0, valW = text_width("9999 WPM", sc);
    for (int i = 0; i < n; i++) {
        int lw = text_width(sv->options[i].label, sc);
        if (lw > labelW) labelW = lw;
    }
    const char *hint = "UP DOWN PICK   LEFT RIGHT CHANGE   ENTER SAVE   O CLOSE";
    int bw = pad * 7 + labelW + rh * 2 + valW + pad * 2, hintW = text_width(hint, 1) + pad * 2;
    if (bw < hintW) bw = hintW;
    if (bw > w - 2 * pad) bw = w - 2 * pad;
    int bh = rh * (n + 3) + pad * 3;
    p->box = (SDL_Rect){ w - bw - 12, 12, bw, bh };
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 170);
    SDL_RenderFillRect(r, &p->box);
    int x0 = p->box.x + pad * 2, y = p->box.y + pad;
    char title[64];
    snprintf(title, sizeof title, "%s SETTINGS", sv->label);
    draw_text_shadow(r, title, x0, y + (rh - FONT_H * sc) / 2, sc, (SDL_Color){ 255, 228, 92, 255 });
    y += rh;
    int restartRows = 0;
    for (int i = 0; i < n; i++, y += rh) {
        const SaverOption *o = &sv->options[i];
        int v = *o->value, changed = v != p->saved[i];
        p->row[i] = (SDL_Rect){ p->box.x + pad, y, bw - 2 * pad, rh };
        if (i == p->sel) {
            SDL_SetRenderDrawColor(r, 255, 255, 255, 45);
            SDL_RenderFillRect(r, &p->row[i]);
        }
        SDL_Color lc = (o->flags & OPT_RESTART) ? (SDL_Color){ 255, 220, 120, 255 } : (SDL_Color){ 255, 255, 255, 255 };
        restartRows |= (o->flags & OPT_RESTART) != 0;
        draw_text_shadow(r, o->label, x0, y + (rh - FONT_H * sc) / 2, sc, lc);
        int bx = p->box.x + bw - pad * 2 - rh * 2 - valW, bsz = rh - 2 * sc;
        p->minus[i] = (SDL_Rect){ bx, y + sc, bsz, bsz };
        p->plus[i] = (SDL_Rect){ bx + rh + valW, y + sc, bsz, bsz };
        small_button(r, p->minus[i], "-", sc, 0);
        small_button(r, p->plus[i], "+", sc, 0);
        char val[32];
        option_format(o, v, val, sizeof val);
        if (changed) strncat(val, "*", sizeof val - strlen(val) - 1);
        SDL_Color vc = changed ? (SDL_Color){ 140, 230, 255, 255 } : (SDL_Color){ 255, 255, 255, 255 };
        draw_text_shadow(r, val, bx + rh + (valW - text_width(val, sc)) / 2 - sc, y + (rh - FONT_H * sc) / 2, sc, vc);
    }
    y += pad;
    int b1 = text_width("DEFAULTS", sc) + 6 * sc;
    p->save = (SDL_Rect){ x0, y, b1, rh - sc };
    p->defaults = (SDL_Rect){ x0 + b1 + pad * 2, y, b1, rh - sc };
    small_button(r, p->save, p->savedFlash > 0 ? "SAVED" : "SAVE", sc, p->savedFlash > 0);
    small_button(r, p->defaults, "DEFAULTS", sc, 0);
    int dirty = 0;
    for (int i = 0; i < n; i++) dirty |= *sv->options[i].value != p->saved[i];
    if (dirty) draw_text_shadow(r, "* NOT SAVED YET", p->defaults.x + b1 + pad * 2, y + (rh - sc - FONT_H * sc) / 2, sc,
                                (SDL_Color){ 140, 230, 255, 255 });
    y += rh + pad;
    draw_text_shadow(r, hint, x0, y, 1, (SDL_Color){ 220, 220, 220, 255 });
    if (restartRows)
        draw_text_shadow(r, "YELLOW ONES START IT OVER", x0, y + 10, 1, (SDL_Color){ 255, 220, 120, 255 });
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

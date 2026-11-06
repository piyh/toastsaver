/* The picker, the app's front page: big live tiles a six-year-old can use. Tap a picture to
   watch that saver right now (any touch comes back; nothing is changed). Tap a tile's heart
   to keep it in your screensaver or take it out. One heart: that saver plays when the
   computer is idle. Several: they take turns. At least one always stays in. The bar under
   the tiles says what will play. The gear in the top-left corner opens the settings page, and
   a saver with settings of its own has a little sliders button on its picture's other corner
   (or press O on its tile) that opens its own settings page.
   On disk this is still SAVER (one id, or "random") and RANDOM_SAVERS (who takes turns). */
#include "engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TILES 32   /* room to grow: the grid lays itself out for SAVER_COUNT tiles */
#define TILES SAVER_COUNT

typedef struct {
    SDL_Rect box, pic, preview;   /* pic: the framed picture under the title */
    float hx, hy, hr;             /* the heart: centre and radius */
    float ox, oy, or;             /* the sliders button for the saver's own settings (or = 0: it has none) */
    const Saver *sv;
    void *st;
} Tile;

typedef struct {
    SDL_Renderer *r;
    int w, h, sel, hover, hoverHeart, quit, keys;   /* keys: the keyboard is in use, show its cursor */
    int deny;                     /* the tile whose heart just refused to go (the last one) */
    int hoverOpts, openOptions;   /* sliders under the pointer; a tile whose settings to open, plus one */
    float t, bump, denyT, titleH, labelH;
    Tile tile[MAX_TILES];
    int cols;
    SDL_Rect bar, nameBox;
    SDL_Point closeC, gearC;
    int closeR;
    int openSettings;
} Picker;

/* ---------- what is kept ---------- */

static int kept(const Saver *sv) {
    return !strcmp(g_config.saver, "random") ? saver_in_mix(sv) : !strcmp(g_config.saver, sv->id);
}

static int kept_count(void) {
    int n = 0;
    for (int i = 0; i < SAVER_COUNT; i++) n += kept(SAVERS[i]);
    return n;
}

static int name_kept(void) { return kept(&saver_name); }

/* Flips one heart. The last heart stays: returns 0 then. */
static int flip_heart(const Saver *sv) {
    int on[MAX_TILES], n = 0, only = -1;
    for (int i = 0; i < SAVER_COUNT; i++) {
        on[i] = SAVERS[i] == sv ? !kept(sv) : kept(SAVERS[i]);
        if (on[i]) { n++; only = i; }
    }
    if (n == 0) return 0;
    if (n == 1) {
        snprintf(g_config.saver, sizeof g_config.saver, "%s", SAVERS[only]->id);
    } else {
        char list[sizeof g_config.randomSavers] = "";
        size_t len = 0;
        for (int i = 0, c = 0; i < SAVER_COUNT; i++)
            if (on[i]) len += (size_t)snprintf(list + len, sizeof list - len, "%s%s", c++ ? "," : "", SAVERS[i]->id);
        if (len >= sizeof list) return 0;
        /* Everything on is written as empty, so savers added later join in too. */
        snprintf(g_config.randomSavers, sizeof g_config.randomSavers, "%s", n == SAVER_COUNT ? "" : list);
        snprintf(g_config.saver, sizeof g_config.saver, "random");
    }
    config_save();
    return 1;
}

/* ---------- layout ---------- */

static void destroy_previews(Picker *p) {
    for (int i = 0; i < TILES; i++)
        if (p->tile[i].st) { p->tile[i].sv->destroy(p->tile[i].st); p->tile[i].st = NULL; }
}

static void layout(Picker *p) {
    destroy_previews(p);
    int W = p->w, H = p->h, m = H / 56 > 4 ? H / 56 : 4;
    p->titleH = H * 0.12f;
    float bottomH = H * 0.14f, gridTop = p->titleH, gridBottom = H - bottomH;
    /* Every preview is 16:9, like the screen it stands for. Try each column count and keep
       the one that gives the biggest previews; the last, shorter row is centered. */
    int pad = H / 150 > 3 ? H / 150 : 3, label = (int)(H * 0.034f);
    int bestCols = 3, bestPw = 0, bestPh = 0;
    for (int c = 2; c <= 8; c++) {
        int rows = (TILES + c - 1) / c;
        int pwMax = (W - (c + 1) * m) / c - 2 * pad;
        int phMax = ((int)(gridBottom - gridTop) - (rows - 1) * m) / rows - pad - label;
        int ph = pwMax * 9 / 16 < phMax ? pwMax * 9 / 16 : phMax, pw = ph * 16 / 9;
        if (pw * ph > bestPw * bestPh) { bestCols = c; bestPw = pw; bestPh = ph; }
    }
    p->cols = bestCols;
    int rows = (TILES + p->cols - 1) / p->cols;
    int tw = bestPw + 2 * pad, th = bestPh + pad + label;
    p->labelH = (float)label;   /* the saver's name goes above its picture, in this row */
    int top = (int)gridTop + ((int)(gridBottom - gridTop) - (rows * th + (rows - 1) * m)) / 2;
    for (int i = 0; i < TILES; i++) {
        Tile *t = &p->tile[i];
        t->sv = SAVERS[i];
        int row = i / p->cols, col = i % p->cols;
        int inRow = row == rows - 1 ? TILES - row * p->cols : p->cols;
        int left = (W - (inRow * tw + (inRow - 1) * m)) / 2;
        t->box = (SDL_Rect){ left + col * (tw + m), top + row * (th + m), tw, th };
        t->pic = (SDL_Rect){ t->box.x + pad / 2, t->box.y + label, tw - pad, bestPh + pad };
        t->preview = (SDL_Rect){ t->pic.x + pad / 2, t->pic.y + pad / 2, bestPw, bestPh };
        t->hr = t->pic.h * 0.13f;                      /* the heart sits on the picture's corner */
        t->hx = t->pic.x + t->pic.w - t->hr * 0.75f;
        t->hy = t->pic.y + t->hr * 0.95f;
        t->or = option_count(t->sv) ? t->hr * 0.8f : 0;   /* the sliders sit on the other top corner */
        t->ox = t->pic.x + t->hr * 0.75f;
        t->oy = t->pic.y + t->hr * 0.95f;
        t->st = t->sv->create(p->r, t->preview.w, t->preview.h);
    }
    int bh = (int)(bottomH * 0.62f), by = (int)gridBottom + (int)(bottomH - bh) / 2;
    p->bar = (SDL_Rect){ W * 8 / 100, by, W * 84 / 100, bh };
    p->nameBox = (SDL_Rect){ W * 66 / 100, by, W * 26 / 100, bh };
    p->closeR = (int)(p->titleH * 0.3f);
    p->closeC = (SDL_Point){ W - m - p->closeR, (int)(p->titleH / 2) };
    p->gearC = (SDL_Point){ m + p->closeR, (int)(p->titleH / 2) };
}

/* With NAME kept, the bar shares the footer with the box where you type the name. */
static SDL_Rect bar_rect(const Picker *p) {
    SDL_Rect b = p->bar;
    if (name_kept()) b.w = p->nameBox.x - b.x - p->w * 2 / 100;
    return b;
}

static void refresh_name_preview(Picker *p) {
    for (int i = 0; i < TILES; i++)
        if (p->tile[i].sv == &saver_name) {
            p->tile[i].sv->destroy(p->tile[i].st);
            p->tile[i].st = p->tile[i].sv->create(p->r, p->tile[i].preview.w, p->tile[i].preview.h);
        }
}

static void heart_tapped(Picker *p, int i) {
    p->sel = i;
    if (!flip_heart(p->tile[i].sv)) { p->deny = i; p->denyT = 0.6f; return; }   /* the last one stays */
    p->bump = 1;
    if (name_kept()) SDL_StartTextInput();
    else SDL_StopTextInput();
}

void *picker_create(SDL_Renderer *r, int w, int h) {
    Picker *p = calloc(1, sizeof *p);
    p->r = r;
    p->w = w;
    p->h = h;
    p->hover = p->hoverHeart = p->hoverOpts = p->deny = -1;
    layout(p);
    for (int i = 0; i < SAVER_COUNT; i++)
        if (!strcmp(g_config.saver, SAVERS[i]->id)) p->sel = i;
    return p;
}

void picker_destroy(void *v) {
    destroy_previews(v);
    free(v);
}

void picker_step(void *v, float dt) {
    Picker *p = v;
    p->t += dt;
    p->bump = p->bump > 0 ? p->bump - dt * 3 : 0;
    p->denyT = p->denyT > 0 ? p->denyT - dt : 0;
    for (int i = 0; i < TILES; i++)
        if (p->tile[i].st) p->tile[i].sv->step(p->tile[i].st, dt);
}

/* ---------- drawing ---------- */

static void heart_shape(SDL_Renderer *r, float cx, float cy, float s, SDL_Color c) {
    fill_circle(r, cx - s * 0.5f, cy - s * 0.25f, s * 0.55f, c);
    fill_circle(r, cx + s * 0.5f, cy - s * 0.25f, s * 0.55f, c);
    fill_triangle(r, cx - s * 1.02f, cy - s * 0.05f, cx + s * 1.02f, cy - s * 0.05f, cx, cy + s * 1.0f, c);
}

static void centered_text(SDL_Renderer *r, const char *s, SDL_Rect box, float height, SDL_Color c) {
    int sc = (int)(height / FONT_H);
    while (sc > 1 && text_width(s, sc) > box.w * 0.92f) sc--;
    if (sc < 1) sc = 1;
    draw_text_shadow(r, s, box.x + (box.w - text_width(s, sc)) / 2, box.y + (box.h - FONT_H * sc) / 2, sc, c);
}

static void draw_tile(Picker *p, SDL_Renderer *r, int i) {
    Tile *t = &p->tile[i];
    int on = kept(t->sv), cur = p->keys && i == p->sel;
    SDL_Rect box = t->pic;
    /* Rings go the same width all round and stay inside the gaps, so they never run into
       the title above or the next row's title below. The frame's corners are only as round
       as the inset allows, so the square picture never pokes out of them. */
    int gap = p->h / 56 > 4 ? p->h / 56 : 4, inset = t->preview.x - box.x;
    int ringMax = (int)(p->labelH * 0.2f) < gap / 2 ? (int)(p->labelH * 0.2f) : gap / 2;
    if (ringMax < 2) ringMax = 2;
    int rad = inset * 3 > 2 ? inset * 3 : 2, ring = 0;
    uint32_t ringCol = 0;
    if (cur) { ring = ringMax; ringCol = 0xFFE45C; }                  /* keyboard cursor */
    else if (i == p->hover) { ring = ringMax; ringCol = 0xFFFFFF; }   /* pointer on the picture: tap to watch */
    else if (on) { ring = ringMax * 6 / 10; ringCol = 0xFF4D7A; }     /* kept: a pink frame */
    if (ring < 1 && !on) { ring = 1; ringCol = 0x4A3F8A; }
    if (ring > 0) fill_rrect(r, (SDL_Rect){ box.x - ring, box.y - ring, box.w + 2 * ring, box.h + 2 * ring }, rad + ring, hexcol(ringCol));
    fill_rrect(r, box, rad, hexcol(0x16133A));

    SDL_RenderSetViewport(r, &t->preview);
    t->sv->draw(t->st, r);
    SDL_RenderSetViewport(r, NULL);
    if (i == p->hover) {                                             /* a play sign says what a tap does */
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
        float cx = t->preview.x + t->preview.w / 2.0f, cy = t->preview.y + t->preview.h / 2.0f, s = t->preview.h * 0.16f;
        fill_circle(r, cx, cy, s * 1.6f, (SDL_Color){ 20, 16, 48, 140 });
        fill_triangle(r, cx - s * 0.55f, cy - s * 0.8f, cx - s * 0.55f, cy + s * 0.8f, cx + s * 0.85f, cy, hexcol(0xFFFFFF));
    }

    SDL_Rect title = { t->box.x, t->box.y, t->box.w, (int)p->labelH };
    centered_text(r, t->sv->label, title, p->labelH * 0.62f, on ? hexcol(0xFFB3C8) : hexcol(0xFFFFFF));

    /* The heart: filled pink when kept, an empty outline when not. It wiggles if it's the last. */
    float hx = t->hx, hy = t->hy, hr = t->hr * (i == p->hoverHeart ? 1.18f : 1.0f);
    if (on && i == p->sel) hr *= 1 + 0.25f * p->bump;
    if (i == p->deny && p->denyT > 0) hx += sinf(p->denyT * 40) * t->hr * 0.25f * (p->denyT / 0.6f);
    fill_circle(r, hx, hy, hr * 1.12f, hexcol(on ? 0xFFFFFF : 0x8088AA));
    fill_circle(r, hx, hy, hr, hexcol(on ? 0xFF4D7A : 0x1A1240));
    heart_shape(r, hx, hy + hr * 0.08f, hr * 0.5f, on ? hexcol(0xFFFFFF) : hexcol(0x8088AA));
    if (!on) heart_shape(r, hx, hy + hr * 0.08f + hr * 0.04f, hr * 0.32f, hexcol(0x1A1240));
}

/* A saver's own settings: three little sliders on a dark button, in the gear's quiet colours. */
static void draw_sliders(SDL_Renderer *r, const Tile *t, int hover) {
    float cx = t->ox, cy = t->oy, rad = t->or * (hover ? 1.18f : 1.0f);
    SDL_Color c = hexcol(0xB8C4FF);
    fill_circle(r, cx, cy, rad * 1.12f, c);
    fill_circle(r, cx, cy, rad, hexcol(0x1A1240));
    static const float KNOB[3] = { -0.3f, 0.35f, -0.05f };
    for (int k = 0; k < 3; k++) {
        float y = cy + (k - 1) * rad * 0.42f;
        thick_line(r, cx - rad * 0.55f, y, cx + rad * 0.55f, y, rad * 0.1f, c);
        fill_circle(r, cx + KNOB[k] * rad, y, rad * 0.15f, c);
    }
}

/* The settings gear, in quiet colours. */
static void draw_gear(Picker *p, SDL_Renderer *r) {
    float cx = (float)p->gearC.x, cy = (float)p->gearC.y, rad = p->closeR * 0.95f;
    SDL_Color c = hexcol(0xB8C4FF);
    for (int k = 0; k < 8; k++) {
        float a = k * PI_F / 4;
        thick_line(r, cx + cosf(a) * rad * 0.5f, cy + sinf(a) * rad * 0.5f,
                   cx + cosf(a) * rad, cy + sinf(a) * rad, rad * 0.3f, c);
    }
    fill_circle(r, cx, cy, rad * 0.72f, c);
    fill_circle(r, cx, cy, rad * 0.3f, hexcol(0x3A1C71));
}

/* "YOUR SCREENSAVER: *VOLCANO", or who takes turns. Long lists just give the count. */
static void bar_text(char *out, size_t n) {
    int count = kept_count();
    if (count == 1) {
        for (int i = 0; i < SAVER_COUNT; i++)
            if (kept(SAVERS[i])) snprintf(out, n, "YOUR SCREENSAVER: *%s", SAVERS[i]->label);
        return;
    }
    if (count > 3) { snprintf(out, n, "YOUR SCREENSAVER: *%d TAKE TURNS", count); return; }
    size_t len = (size_t)snprintf(out, n, "TAKING TURNS:");
    for (int i = 0, c = 0; i < SAVER_COUNT && len < n; i++)
        if (kept(SAVERS[i])) len += (size_t)snprintf(out + len, n - len, "%s *%s", c++ ? "," : "", SAVERS[i]->label);
}

void picker_draw(void *v, SDL_Renderer *r) {
    Picker *p = v;
    SDL_RenderSetViewport(r, NULL);
    vgradient(r, p->w, p->h, hexcol(0x3A1C71), hexcol(0x0F4C81));

    int tpad = p->closeR * 3;   /* keep the title between the gear and the X */
    SDL_Rect title = { tpad, 0, p->w - 2 * tpad, (int)p->titleH };
    centered_text(r, "TAP TO WATCH  * TO KEEP", title, p->titleH * 0.42f, hexcol(0xFFE45C));

    fill_circle(r, (float)p->closeC.x, (float)p->closeC.y, p->closeR * 1.12f, hexcol(0xFFFFFF));
    fill_circle(r, (float)p->closeC.x, (float)p->closeC.y, (float)p->closeR, hexcol(0xE53935));
    float k = p->closeR * 0.45f;
    thick_line(r, p->closeC.x - k, p->closeC.y - k, p->closeC.x + k, p->closeC.y + k, p->closeR * 0.28f, hexcol(0xFFFFFF));
    thick_line(r, p->closeC.x - k, p->closeC.y + k, p->closeC.x + k, p->closeC.y - k, p->closeR * 0.28f, hexcol(0xFFFFFF));

    for (int i = 0; i < TILES; i++) {
        draw_tile(p, r, i);
        if (p->tile[i].or > 0) draw_sliders(r, &p->tile[i], i == p->hoverOpts);
    }
    draw_gear(p, r);

    SDL_Rect b = bar_rect(p);                                       /* what will play */
    fill_rrect(r, b, b.h / 3, hexcol(0x1A1240));
    char text[160];
    bar_text(text, sizeof text);
    centered_text(r, text, (SDL_Rect){ b.x + b.h / 4, b.y, b.w - b.h / 2, b.h }, b.h * 0.34f, hexcol(0xFFFFFF));

    if (name_kept()) {
        SDL_Rect nb = p->nameBox;
        fill_rrect(r, nb, nb.h / 4, hexcol(0xFFFFFF));
        SDL_Rect cap = { nb.x, nb.y + nb.h / 14, nb.w, nb.h * 3 / 10 };
        SDL_Rect line = { nb.x, cap.y + cap.h, nb.w, nb.h - (cap.y + cap.h - nb.y) - nb.h / 14 };
        int cs = cap.h * 7 / 10 / FONT_H;
        while (cs > 1 && text_width("TYPE YOUR NAME:", cs) > cap.w * 0.85f) cs--;
        if (cs < 1) cs = 1;
        draw_text(r, "TYPE YOUR NAME:", cap.x + (cap.w - text_width("TYPE YOUR NAME:", cs)) / 2,
                  cap.y + (cap.h - FONT_H * cs) / 2, cs, hexcol(0x8E24AA));
        char shown[24];
        int blink = (int)(p->t * 2) % 2;
        snprintf(shown, sizeof shown, "%s%s", g_config.name, blink ? "-" : " ");
        centered_text(r, shown, line, line.h * 0.7f, hexcol(0x3A1C71));
    }
}

/* ---------- input ---------- */

static int hit(SDL_Rect rc, int x, int y) { return x >= rc.x && y >= rc.y && x < rc.x + rc.w && y < rc.y + rc.h; }

/* Runs one saver full screen. Touches nothing: not the hearts, not the config. */
static void watch(Picker *p, SDL_Window *win, int i) {
    /* Someone is watching on purpose, so hold off the idle watcher like a video player would;
       otherwise after IDLE_MINUTES it starts the kept saver over the top of this one. */
    SDL_DisableScreenSaver();
    if (run_saver(win, p->r, p->tile[i].sv, 0, 0) == 1) p->quit = 1;
    SDL_EnableScreenSaver();
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
    p->hover = p->hoverHeart = -1;
}

/* Which tile's heart is at (x, y); -1 if none. A little generous, for small fingers. */
int picker_hit_heart(void *v, int x, int y) {
    Picker *p = v;
    for (int i = 0; i < TILES; i++) {
        const Tile *t = &p->tile[i];
        float dx = x - t->hx, dy = y - t->hy, rr = t->hr * 1.5f;
        if (dx * dx + dy * dy <= rr * rr) return i;
    }
    return -1;
}

/* Which tile's sliders button (its own settings) is at (x, y); -1 if none. */
static int hit_sliders(const Picker *p, int x, int y) {
    for (int i = 0; i < TILES; i++) {
        const Tile *t = &p->tile[i];
        float dx = x - t->ox, dy = y - t->oy, rr = t->or * 1.4f;
        if (t->or > 0 && dx * dx + dy * dy <= rr * rr) return i;
    }
    return -1;
}

/* Which tile's picture (or title) is at (x, y), not counting its heart or sliders; -1 if none. */
int picker_hit_picture(void *v, int x, int y) {
    Picker *p = v;
    if (picker_hit_heart(p, x, y) >= 0 || hit_sliders(p, x, y) >= 0) return -1;
    for (int i = 0; i < TILES; i++) if (hit(p->tile[i].box, x, y)) return i;
    return -1;
}

static void click(Picker *p, SDL_Window *win, int x, int y) {
    int dx = x - p->closeC.x, dy = y - p->closeC.y;
    if (dx * dx + dy * dy <= p->closeR * p->closeR * 1.6f) { p->quit = 1; return; }
    dx = x - p->gearC.x; dy = y - p->gearC.y;
    if (dx * dx + dy * dy <= p->closeR * p->closeR * 1.6f) { p->openSettings = 1; return; }
    p->keys = 0;
    int i = picker_hit_heart(p, x, y);
    if (i >= 0) { heart_tapped(p, i); return; }
    i = hit_sliders(p, x, y);
    if (i >= 0) { p->sel = i; p->openOptions = i + 1; return; }
    i = picker_hit_picture(p, x, y);
    if (i >= 0) { p->sel = i; watch(p, win, i); }
}

static void move_cursor(Picker *p, int i) {
    if (i >= 0 && i < TILES) p->sel = i;
    p->keys = 1;
}

int run_picker(SDL_Window *win, SDL_Renderer *r) {
    int w, h;
    SDL_GetRendererOutputSize(r, &w, &h);
    Picker *p = picker_create(r, w, h);
    int ww, wh;
    SDL_GetWindowSize(win, &ww, &wh);
    float sx = (float)w / ww, sy = (float)h / wh;   /* mouse is in window units, drawing in pixels */
    if (name_kept()) SDL_StartTextInput(); else SDL_StopTextInput();
    Uint64 last = SDL_GetPerformanceCounter();
    while (!p->quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: p->quit = 1; break;
            case SDL_MOUSEMOTION: {
                int x = (int)(e.motion.x * sx), y = (int)(e.motion.y * sy);
                p->hoverHeart = picker_hit_heart(p, x, y);
                p->hover = picker_hit_picture(p, x, y);
                p->hoverOpts = hit_sliders(p, x, y);
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) click(p, win, (int)(e.button.x * sx), (int)(e.button.y * sy));
                break;
            case SDL_TEXTINPUT:
                if (name_kept()) {
                    char buf[64];
                    snprintf(buf, sizeof buf, "%s%s", g_config.name, e.text.text);
                    config_set_name(buf);
                    config_save();
                    refresh_name_preview(p);
                }
                break;
            case SDL_KEYDOWN: {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_ESCAPE) p->quit = 1;
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) watch(p, win, p->sel);
                else if (k == SDLK_SPACE && !name_kept()) { p->keys = 1; heart_tapped(p, p->sel); }
                else if (k == SDLK_o && !name_kept() && p->tile[p->sel].or > 0) p->openOptions = p->sel + 1;
                else if (k == SDLK_LEFT) move_cursor(p, p->sel - 1);
                else if (k == SDLK_RIGHT) move_cursor(p, p->sel + 1);
                else if (k == SDLK_UP) move_cursor(p, p->sel >= p->cols ? p->sel - p->cols : p->sel);
                else if (k == SDLK_DOWN) move_cursor(p, p->sel + p->cols < TILES ? p->sel + p->cols : p->sel);
                else if (k == SDLK_BACKSPACE && name_kept() && g_config.name[0]) {
                    g_config.name[strlen(g_config.name) - 1] = 0;
                    config_save();
                    refresh_name_preview(p);
                }
                break;
            }
            case SDL_WINDOWEVENT:
                if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                    SDL_GetRendererOutputSize(r, &p->w, &p->h);
                    SDL_GetWindowSize(win, &ww, &wh);
                    sx = (float)p->w / ww;
                    sy = (float)p->h / wh;
                    layout(p);
                }
                break;
            }
        }
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = (float)(now - last) / (float)SDL_GetPerformanceFrequency();
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        picker_step(p, dt);
        if (p->openOptions) {                         /* a saver's own settings, from its tile */
            Tile *t = &p->tile[p->openOptions - 1];
            p->openOptions = 0;
            SDL_StopTextInput();
            if (run_saver_options(win, r, t->sv) == 1) p->quit = 1;
            SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
            t->sv->destroy(t->st);                    /* start its preview over with the new settings */
            t->st = t->sv->create(r, t->preview.w, t->preview.h);
            p->hover = p->hoverHeart = p->hoverOpts = -1;
            if (name_kept()) SDL_StartTextInput();
            last = SDL_GetPerformanceCounter();
            continue;
        }
        if (p->openSettings) {
            p->openSettings = 0;
            SDL_StopTextInput();
            run_settings(win, r);
            SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
            if (name_kept()) SDL_StartTextInput();
            last = SDL_GetPerformanceCounter();
            continue;
        }
        picker_draw(p, r);
        SDL_RenderPresent(r);
        SDL_Delay(10);
    }
    SDL_StopTextInput();
    picker_destroy(p);
    return 0;
}

/* The picker, the app's front page: big live tiles a six-year-old can use. Tap a tile to
   choose it (saved immediately), tap it again or press TRY IT to watch it. SURPRISE ME
   (below the tiles) switches to a random saver each time; while it is on, tapping a
   tile puts it in or takes it out of the surprise mix. The gear in the top-left corner
   opens the settings page in the same window. */
#include "engine.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_TILES 16
#define TILES SAVER_COUNT

typedef struct {
    SDL_Rect box, preview;
    const Saver *sv;
    void *st;
} Tile;

typedef struct {
    SDL_Renderer *r;
    int w, h, sel, hover, quit;
    int surprise;          /* SAVER=random; sel is then just the keyboard cursor */
    float t, bump, titleH, labelH;
    Tile tile[MAX_TILES];
    int cols;
    SDL_Rect tryBtn, nameBox, surpriseBtn;
    SDL_Point closeC, gearC;
    int closeR;
    int openSettings;
} Picker;

static int name_selected(const Picker *p) { return !p->surprise && p->tile[p->sel].sv == &saver_name; }

static void destroy_previews(Picker *p) {
    for (int i = 0; i < TILES; i++)
        if (p->tile[i].st) { p->tile[i].sv->destroy(p->tile[i].st); p->tile[i].st = NULL; }
}

static void layout(Picker *p) {
    destroy_previews(p);
    int W = p->w, H = p->h, m = H / 32 > 6 ? H / 32 : 6;
    p->titleH = H * 0.13f;
    float bottomH = H * 0.17f, gridTop = p->titleH, gridBottom = H - bottomH;
    /* 3 columns while it fits in a 3x3 grid, then 4. The last row is centered. */
    p->cols = TILES > 9 ? 4 : 3;
    int rows = (TILES + p->cols - 1) / p->cols;
    int tw = (W - 2 * m - (p->cols - 1) * m) / p->cols, th = (int)((gridBottom - gridTop - (rows - 1) * m) / rows);
    p->labelH = th * 0.22f;
    int pad = th / 22 > 4 ? th / 22 : 4;
    for (int i = 0; i < TILES; i++) {
        Tile *t = &p->tile[i];
        t->sv = SAVERS[i];
        int row = i / p->cols, col = i % p->cols;
        int inRow = row == rows - 1 ? TILES - row * p->cols : p->cols;
        int left = m + (p->cols - inRow) * (tw + m) / 2;
        t->box = (SDL_Rect){ left + col * (tw + m), (int)gridTop + row * (th + m), tw, th };
        t->preview = (SDL_Rect){ t->box.x + pad, t->box.y + pad, tw - 2 * pad, th - (int)p->labelH - pad };
        t->st = t->sv->create(p->r, t->preview.w, t->preview.h);
    }
    int bh = (int)(bottomH * 0.62f), by = (int)gridBottom + (int)(bottomH - bh) / 2;
    p->surpriseBtn = (SDL_Rect){ 0, by, W * 29 / 100, bh };
    p->tryBtn = (SDL_Rect){ 0, by, W * 3 / 10, bh };
    p->nameBox = (SDL_Rect){ W * 68 / 100, by, W * 28 / 100, bh };
    p->closeR = (int)(p->titleH * 0.3f);
    p->closeC = (SDL_Point){ W - m - p->closeR, (int)(p->titleH / 2) };
    p->gearC = (SDL_Point){ m + p->closeR, (int)(p->titleH / 2) };
}

static void save_choice(Picker *p) {
    snprintf(g_config.saver, sizeof g_config.saver, "%s", p->surprise ? "random" : p->tile[p->sel].sv->id);
    config_save();
    if (name_selected(p)) SDL_StartTextInput();
    else SDL_StopTextInput();
}

/* Arrow keys and taps both land here. With SURPRISE on they only move the cursor. */
/* Footer: SURPRISE ME and TRY IT side by side in the middle. When NAME is chosen
   they move left and center to make room for the name box on the right. */
static void place_footer(Picker *p) {
    int W = p->w, gap = W * 3 / 100;
    p->surpriseBtn.x = name_selected(p) ? W * 4 / 100 : (W - p->surpriseBtn.w - gap - p->tryBtn.w) / 2;
    p->tryBtn.x = name_selected(p) ? (W - p->tryBtn.w) / 2 : p->surpriseBtn.x + p->surpriseBtn.w + gap;
}

static void select_tile(Picker *p, int i) {
    if (i < 0 || i >= TILES || (p->surprise && i == p->sel)) return;
    p->sel = i;
    p->bump = 1;
    if (!p->surprise) save_choice(p);
}

static void toggle_surprise(Picker *p) {
    p->surprise = !p->surprise;
    p->bump = 1;
    save_choice(p);
}

/* Puts a saver in or out of the surprise mix. The last one can't be taken out. */
static void toggle_mix(Picker *p, int i) {
    p->sel = i;
    p->bump = 1;
    if (saver_set_in_mix(p->tile[i].sv, !saver_in_mix(p->tile[i].sv))) config_save();
}

static void refresh_name_preview(Picker *p) {
    for (int i = 0; i < TILES; i++)
        if (p->tile[i].sv == &saver_name) {
            p->tile[i].sv->destroy(p->tile[i].st);
            p->tile[i].st = p->tile[i].sv->create(p->r, p->tile[i].preview.w, p->tile[i].preview.h);
        }
}

void *picker_create(SDL_Renderer *r, int w, int h) {
    Picker *p = calloc(1, sizeof *p);
    p->r = r;
    p->w = w;
    p->h = h;
    p->hover = -1;
    layout(p);
    p->surprise = !strcmp(g_config.saver, "random");
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
    for (int i = 0; i < TILES; i++)
        if (p->tile[i].st) p->tile[i].sv->step(p->tile[i].st, dt);
}

/* The SURPRISE ME die: it bounces and keeps changing its number while SURPRISE is on. */
static void draw_dice(Picker *p, SDL_Rect area, int rolling) {
    int s = (int)((area.w < area.h ? area.w : area.h) * 0.8f);
    int bounce = rolling ? (int)(fabsf(sinf(p->t * 4)) * area.h * 0.1f) : 0;
    SDL_Rect d = { area.x + (area.w - s) / 2, area.y + (area.h - s) / 2 - bounce, s, s };
    fill_rrect(p->r, (SDL_Rect){ d.x, d.y + s / 12, s, s }, s / 6, hexcol(0x9AA0C8));
    fill_rrect(p->r, d, s / 6, hexcol(0xFFFFFF));
    static const float pips[7][7][2] = {
        {{0}}, {{.5f,.5f}}, {{.25f,.25f},{.75f,.75f}}, {{.25f,.25f},{.5f,.5f},{.75f,.75f}},
        {{.25f,.25f},{.75f,.25f},{.25f,.75f},{.75f,.75f}},
        {{.25f,.25f},{.75f,.25f},{.5f,.5f},{.25f,.75f},{.75f,.75f}},
        {{.25f,.25f},{.75f,.25f},{.25f,.5f},{.75f,.5f},{.25f,.75f},{.75f,.75f}},
    };
    int face = rolling ? 1 + (int)(p->t * 2.5f) % 6 : 5;
    for (int k = 0; k < face; k++)
        fill_circle(p->r, d.x + pips[face][k][0] * s, d.y + pips[face][k][1] * s, s * 0.09f, hexcol(0x222244));
}

/* The empty ring on a tile that is out of the surprise mix. */
static void empty_badge(SDL_Renderer *r, float cx, float cy, float rad) {
    fill_circle(r, cx, cy, rad + rad * 0.15f, hexcol(0xFFFFFF));
    fill_circle(r, cx, cy, rad * 0.8f, hexcol(0x4A4E6A));
}

static void check_badge(SDL_Renderer *r, float cx, float cy, float rad) {
    fill_circle(r, cx, cy, rad + rad * 0.15f, hexcol(0xFFFFFF));
    fill_circle(r, cx, cy, rad, hexcol(0x2E9E44));
    SDL_Color w = hexcol(0xFFFFFF);
    thick_line(r, cx - rad * 0.45f, cy, cx - rad * 0.1f, cy + rad * 0.38f, rad * 0.24f, w);
    thick_line(r, cx - rad * 0.1f, cy + rad * 0.38f, cx + rad * 0.5f, cy - rad * 0.36f, rad * 0.24f, w);
}

static void centered_text(SDL_Renderer *r, const char *s, SDL_Rect box, float height, SDL_Color c) {
    int sc = (int)(height / FONT_H);
    while (sc > 1 && text_width(s, sc) > box.w * 0.92f) sc--;
    if (sc < 1) sc = 1;
    draw_text_shadow(r, s, box.x + (box.w - text_width(s, sc)) / 2, box.y + (box.h - FONT_H * sc) / 2, sc, c);
}

/* A chunky button with a darker shadow underneath, like TRY IT. */
static void push_button(SDL_Renderer *r, SDL_Rect b, uint32_t face, uint32_t shadow) {
    fill_rrect(r, (SDL_Rect){ b.x, b.y + b.h / 12, b.w, b.h }, b.h / 3, hexcol(shadow));
    fill_rrect(r, b, b.h / 3, hexcol(face));
}

static void draw_tile(Picker *p, SDL_Renderer *r, int i) {
    Tile *t = &p->tile[i];
    int cur = i == p->sel, inMix = saver_in_mix(t->sv);
    int lit = p->surprise ? inMix : cur;   /* lit tiles get the check mark */
    SDL_Rect box = t->box;
    if (cur && !p->surprise) {                         /* chosen tile pops out a little */
        int grow = (int)(box.h * (0.03f + 0.04f * p->bump));
        box = (SDL_Rect){ box.x - grow, box.y - grow, box.w + 2 * grow, box.h + 2 * grow };
        int glow = (int)(box.h * 0.035f * (1.0f + 0.3f * sinf(p->t * 5)));
        SDL_Rect outer = { box.x - glow, box.y - glow, box.w + 2 * glow, box.h + 2 * glow };
        fill_rrect(r, outer, box.h / 8, hexcol(0xFFE45C));
    } else if (p->surprise && (cur || inMix)) {
        if (cur) {                                     /* keyboard cursor / just tapped */
            int grow = (int)(box.h * 0.04f * p->bump);
            box = (SDL_Rect){ box.x - grow, box.y - grow, box.w + 2 * grow, box.h + 2 * grow };
        }
        int ring = (int)(box.h * (cur ? 0.045f : 0.03f));
        SDL_Rect outer = { box.x - ring, box.y - ring, box.w + 2 * ring, box.h + 2 * ring };
        fill_rrect(r, outer, box.h / 8, cur ? hexcol(0xFFE45C) : hexcol(0xFF8A3D));
    } else if (i == p->hover) {
        SDL_Rect outer = { box.x - 3, box.y - 3, box.w + 6, box.h + 6 };
        fill_rrect(r, outer, box.h / 8, hexcol(0xB8C4FF));
    }
    fill_rrect(r, box, box.h / 9, hexcol(0x16133A));

    SDL_RenderSetViewport(r, &t->preview);
    t->sv->draw(t->st, r);
    SDL_RenderSetViewport(r, NULL);
    if (p->surprise && !inMix) {                       /* out of the mix: dimmed */
        SDL_SetRenderDrawColor(r, 0x16, 0x13, 0x3A, 175);
        SDL_RenderFillRect(r, &t->preview);
    }

    SDL_Rect label = { t->box.x, t->box.y + t->box.h - (int)p->labelH, t->box.w, (int)p->labelH };
    SDL_Color lc = p->surprise ? (inMix ? hexcol(0xFFFFFF) : hexcol(0x8088AA))
                               : (cur ? hexcol(0xFFE45C) : hexcol(0xFFFFFF));
    centered_text(r, t->sv->label, label, p->labelH * 0.62f, lc);
    float bx = (float)(box.x + box.w - box.h * 0.06f), by = (float)(box.y + box.h * 0.06f), br = box.h * 0.11f;
    if (lit) check_badge(r, bx, by, br);
    else if (p->surprise) empty_badge(r, bx, by, br);
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

void picker_draw(void *v, SDL_Renderer *r) {
    Picker *p = v;
    SDL_RenderSetViewport(r, NULL);
    vgradient(r, p->w, p->h, hexcol(0x3A1C71), hexcol(0x0F4C81));

    SDL_Rect title = { 0, 0, p->w, (int)p->titleH };
    centered_text(r, p->surprise ? "PICK YOUR SURPRISE MIX!" : "PICK A SCREENSAVER!", title,
                  p->titleH * 0.5f, hexcol(0xFFE45C));

    fill_circle(r, (float)p->closeC.x, (float)p->closeC.y, p->closeR * 1.12f, hexcol(0xFFFFFF));
    fill_circle(r, (float)p->closeC.x, (float)p->closeC.y, (float)p->closeR, hexcol(0xE53935));
    float k = p->closeR * 0.45f;
    thick_line(r, p->closeC.x - k, p->closeC.y - k, p->closeC.x + k, p->closeC.y + k, p->closeR * 0.28f, hexcol(0xFFFFFF));
    thick_line(r, p->closeC.x - k, p->closeC.y + k, p->closeC.x + k, p->closeC.y - k, p->closeR * 0.28f, hexcol(0xFFFFFF));

    for (int i = 0; i < TILES; i++) draw_tile(p, r, i);
    draw_gear(p, r);
    place_footer(p);

    /* SURPRISE ME: a toggle. On, it glows like a chosen tile and its die rolls. */
    SDL_Rect sb = p->surpriseBtn;
    if (p->surprise) {
        int glow = (int)(sb.h * 0.08f * (1.0f + 0.3f * sinf(p->t * 5)));
        fill_rrect(r, (SDL_Rect){ sb.x - glow, sb.y - glow, sb.w + 2 * glow, sb.h + sb.h / 12 + 2 * glow },
                   sb.h / 3 + glow, hexcol(0xFFE45C));
        push_button(r, sb, 0xFF6F3C, 0xB23A12);
    } else {
        push_button(r, sb, 0x8E24AA, 0x5E1675);
    }
    draw_dice(p, (SDL_Rect){ sb.x + sb.h / 6, sb.y, sb.h, sb.h }, p->surprise);
    SDL_Rect st = { sb.x + sb.h * 7 / 6, sb.y, sb.w - sb.h * 7 / 6 - sb.h / 8, sb.h };
    centered_text(r, "SURPRISE ME!", st, sb.h * 0.36f, hexcol(0xFFFFFF));
    if (p->surprise) check_badge(r, (float)(sb.x + sb.w - sb.h * 0.08f), (float)(sb.y + sb.h * 0.08f), sb.h * 0.2f);

    SDL_Rect btn = p->tryBtn;
    float pulse = 1.0f + 0.03f * sinf(p->t * 4);
    SDL_Rect pb = { btn.x + (int)(btn.w * (1 - pulse) / 2), btn.y + (int)(btn.h * (1 - pulse) / 2),
                    (int)(btn.w * pulse), (int)(btn.h * pulse) };
    push_button(r, pb, 0x43A047, 0x1B5E20);
    float ts = pb.h * 0.32f, tx = pb.x + pb.h * 0.45f, ty = pb.y + pb.h / 2.0f;
    fill_triangle(r, tx, ty - ts, tx, ty + ts, tx + ts * 1.6f, ty, hexcol(0xFFFFFF));
    SDL_Rect txt = { pb.x + (int)(pb.h * 0.6f), pb.y, pb.w - (int)(pb.h * 0.6f), pb.h };
    centered_text(r, "TRY IT!", txt, pb.h * 0.42f, hexcol(0xFFFFFF));

    if (name_selected(p)) {
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

static int hit(SDL_Rect rc, int x, int y) { return x >= rc.x && y >= rc.y && x < rc.x + rc.w && y < rc.y + rc.h; }

static void try_it(Picker *p, SDL_Window *win) {
    const Saver *sv = p->surprise ? saver_resolve("random") : p->tile[p->sel].sv;
    if (run_saver(win, p->r, sv, 0) == 1) p->quit = 1;
    SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
}

static void click(Picker *p, SDL_Window *win, int x, int y) {
    int dx = x - p->closeC.x, dy = y - p->closeC.y;
    if (dx * dx + dy * dy <= p->closeR * p->closeR * 1.6f) { p->quit = 1; return; }
    dx = x - p->gearC.x; dy = y - p->gearC.y;
    if (dx * dx + dy * dy <= p->closeR * p->closeR * 1.6f) { p->openSettings = 1; return; }
    place_footer(p);
    if (hit(p->tryBtn, x, y)) { try_it(p, win); return; }
    if (hit(p->surpriseBtn, x, y)) { toggle_surprise(p); return; }
    for (int i = 0; i < TILES; i++)
        if (hit(p->tile[i].box, x, y)) {
            if (p->surprise) toggle_mix(p, i);
            else if (i == p->sel) try_it(p, win);   /* tap the chosen one again to watch it */
            else select_tile(p, i);
            return;
        }
}

int run_picker(SDL_Window *win, SDL_Renderer *r) {
    int w, h;
    SDL_GetRendererOutputSize(r, &w, &h);
    Picker *p = picker_create(r, w, h);
    int ww, wh;
    SDL_GetWindowSize(win, &ww, &wh);
    float sx = (float)w / ww, sy = (float)h / wh;   /* mouse is in window units, drawing in pixels */
    if (name_selected(p)) SDL_StartTextInput(); else SDL_StopTextInput();
    Uint64 last = SDL_GetPerformanceCounter();
    while (!p->quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_QUIT: p->quit = 1; break;
            case SDL_MOUSEMOTION: {
                int x = (int)(e.motion.x * sx), y = (int)(e.motion.y * sy);
                p->hover = -1;
                for (int i = 0; i < TILES; i++) if (hit(p->tile[i].box, x, y)) p->hover = i;
                break;
            }
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) click(p, win, (int)(e.button.x * sx), (int)(e.button.y * sy));
                break;
            case SDL_TEXTINPUT:
                if (name_selected(p)) {
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
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) try_it(p, win);
                else if (k == SDLK_TAB) toggle_surprise(p);
                else if (k == SDLK_SPACE && p->surprise) toggle_mix(p, p->sel);
                else if (k == SDLK_SPACE && !name_selected(p)) try_it(p, win);
                else if (k == SDLK_LEFT) select_tile(p, p->sel > 0 ? p->sel - 1 : p->sel);
                else if (k == SDLK_RIGHT) select_tile(p, p->sel < TILES - 1 ? p->sel + 1 : p->sel);
                else if (k == SDLK_UP) select_tile(p, p->sel >= p->cols ? p->sel - p->cols : p->sel);
                else if (k == SDLK_DOWN) select_tile(p, p->sel + p->cols < TILES ? p->sel + p->cols : p->sel);
                else if (k == SDLK_BACKSPACE && name_selected(p) && g_config.name[0]) {
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
        if (p->openSettings) {
            p->openSettings = 0;
            SDL_StopTextInput();
            run_settings(win, r);
            SDL_FlushEvents(SDL_FIRSTEVENT, SDL_LASTEVENT);
            if (name_selected(p)) SDL_StartTextInput();
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
